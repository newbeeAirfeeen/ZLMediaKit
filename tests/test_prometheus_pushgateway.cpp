/*
 * author: oaho
 * date: 2026/04/29
 * description: Integration test for the PushGateway client. Boots an in-process
 *              mock gateway (HttpSession + NoticeCenter listener) on a random
 *              port, configures the PushGateway to point at it, then verifies
 *              POSTs arrive with the expected URL/body and that failure replies
 *              do not stop subsequent pushes. Logs are tee'd to stdout and a
 *              memory channel so the WarnL on failure shows up in test output.
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "Common/config.h"
#include "Http/HttpSession.h"
#include "Network/TcpServer.h"
#include "Poller/EventPoller.h"
#include "Util/NoticeCenter.h"
#include "Util/SSLBox.h"
#include "Util/logger.h"
#include "Util/mini.h"
#include "Util/onceToken.h"

#include "Prometheus/Collector.h"
#include "Prometheus/PushGateway.h"

using namespace std;
using namespace toolkit;
using namespace mediakit;

namespace {

// ---------- 测试日志 channel: stdout + 内存截获 ----------
class TeeLogChannel : public LogChannel {
public:
    TeeLogChannel() : LogChannel("TeeLogChannel", LTrace) {}

    void write(const Logger &logger, const LogContextPtr &ctx) override {
        ostringstream oss;
        format(logger, oss, ctx);
        const string line = oss.str();
        cout << line << flush;
        std::lock_guard<std::mutex> lk(_mtx);
        _lines.push_back(line);
    }

    bool anyLineContains(const string &needle) const {
        std::lock_guard<std::mutex> lk(_mtx);
        for (auto &l : _lines) {
            if (l.find(needle) != string::npos) return true;
        }
        return false;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(_mtx);
        return _lines.size();
    }

private:
    mutable std::mutex _mtx;
    vector<string> _lines;
};

// ---------- 微型断言 ----------
int g_failures = 0;
#define EXPECT(cond, msg)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " " << msg << " (" #cond ")" << endl; \
            ++g_failures;                                                                            \
        } else {                                                                                     \
            cout << "[ OK ] " << msg << endl;                                                        \
        }                                                                                            \
    } while (0)

// ---------- mock Pushgateway: 拦截 kBroadcastHttpRequest, 记录请求 ----------
struct MockRequest {
    string url;
    string body;
    string content_type;
};

class MockGateway {
public:
    MockGateway() {
        NoticeCenter::Instance().addListener(
            this, Broadcast::kBroadcastHttpRequest,
            [this](BroadcastHttpRequestArgs) {
                if (parser.Url().find("/metrics/job/") != 0) {
                    return;
                }
                consumed = true;
                MockRequest req;
                req.url = parser.Url();
                req.body = parser.Content();
                req.content_type = parser["Content-Type"];
                {
                    std::lock_guard<std::mutex> lk(_mtx);
                    _requests.push_back(req);
                }
                int status = _status_code.load();
                HttpSession::KeyValue h;
                h.emplace("Content-Type", "text/plain");
                if (status >= 200 && status < 300) {
                    invoker(status, h, string("ok\n"));
                } else {
                    invoker(status, h, string("mock failure\n"));
                }
            });
    }
    ~MockGateway() {
        NoticeCenter::Instance().delListener(this, Broadcast::kBroadcastHttpRequest);
    }

    void setStatus(int status) { _status_code.store(status); }

    vector<MockRequest> snapshot() {
        std::lock_guard<std::mutex> lk(_mtx);
        return _requests;
    }
    void clear() {
        std::lock_guard<std::mutex> lk(_mtx);
        _requests.clear();
    }
    size_t count() {
        std::lock_guard<std::mutex> lk(_mtx);
        return _requests.size();
    }

private:
    std::mutex _mtx;
    vector<MockRequest> _requests;
    std::atomic<int> _status_code{200};
};

}  // namespace

int main() {
    auto tee = make_shared<TeeLogChannel>();
    Logger::Instance().add(tee);
    Logger::Instance().setWriter(make_shared<AsyncLogWriter>());

    constexpr uint16_t kPort = 28200;
    mINI::Instance()["http.port"] = kPort;
    mINI::Instance()["http.sslport"] = 0;
    mINI::Instance()[Prometheus::kEnable] = 1;
    mINI::Instance()[Prometheus::kPath] = "/metrics";
    mINI::Instance()[Prometheus::kAuth] = 0;
    mINI::Instance()[Prometheus::kPushIntervalSec] = 1;
    mINI::Instance()[Prometheus::kPushJob] = "zlmediakit_test";
    mINI::Instance()[Prometheus::kPushInstance] = "test_instance";

    // mock gateway 复用同一个 HTTP server (它通过 NoticeCenter 拦截 /metrics/job/* 路径)
    TcpServer::Ptr http_srv(new TcpServer());
    http_srv->start<HttpSession>(kPort);
    MockGateway mock;

    Prometheus::Collector::Instance().init();

    cout << "===== test 1: push_url 空 → 不应有任何请求 =====" << endl;
    {
        mINI::Instance()[Prometheus::kPushUrl] = "";
        mock.clear();
        Prometheus::PushGateway::Instance().start();
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        EXPECT(mock.count() == 0, "no requests when push_url empty (count=" + to_string(mock.count()) + ")");
        EXPECT(!Prometheus::PushGateway::Instance().running(), "PushGateway not running with empty url");
    }

    cout << "===== test 2: push_url 配置后 → 应有周期性 POST =====" << endl;
    {
        mINI::Instance()[Prometheus::kPushUrl] = "http://127.0.0.1:" + to_string(kPort);
        mock.clear();
        mock.setStatus(200);
        Prometheus::PushGateway::Instance().start();
        // 间隔 1s, 等 2.5s 应至少收到 2 个请求
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        auto reqs = mock.snapshot();
        EXPECT(reqs.size() >= 2, "received at least 2 POSTs (got " + to_string(reqs.size()) + ")");
        if (!reqs.empty()) {
            const auto &first = reqs.front();
            EXPECT(first.url == "/metrics/job/zlmediakit_test/instance/test_instance",
                   "URL has expected job/instance path: " + first.url);
            EXPECT(first.content_type.find("text/plain") != string::npos,
                   "Content-Type is text/plain: " + first.content_type);
            EXPECT(first.body.find("zlm_build_info") != string::npos,
                   "body contains zlm_build_info");
            EXPECT(first.body.find("zlm_streams") != string::npos,
                   "body contains zlm_streams");
        }
        EXPECT(Prometheus::Collector::Instance().totalPushAttempts() >= 2,
               "Collector recorded >= 2 push attempts (got " + to_string(Prometheus::Collector::Instance().totalPushAttempts()) + ")");
        EXPECT(Prometheus::Collector::Instance().totalPushFailures() == 0,
               "no failures recorded (got " + to_string(Prometheus::Collector::Instance().totalPushFailures()) + ")");
        Prometheus::PushGateway::Instance().stop();
    }

    cout << "===== test 3: mock 改返回 503 → push 继续, WarnL 出现 =====" << endl;
    {
        mock.clear();
        mock.setStatus(503);
        // 不清 Collector 累计计数, 直接续测
        const uint64_t failures_before = Prometheus::Collector::Instance().totalPushFailures();
        Prometheus::PushGateway::Instance().start();
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        auto reqs = mock.snapshot();
        EXPECT(reqs.size() >= 2, "still receiving POSTs under 503 (got " + to_string(reqs.size()) + ")");
        const uint64_t failures_after = Prometheus::Collector::Instance().totalPushFailures();
        EXPECT(failures_after - failures_before >= 2,
               "failure counter advanced (delta=" + to_string(failures_after - failures_before) + ")");
        EXPECT(tee->anyLineContains("status=503"),
               "WarnL contains status=503");
        EXPECT(tee->anyLineContains("pushgateway POST failed"),
               "WarnL contains 'pushgateway POST failed'");
        Prometheus::PushGateway::Instance().stop();
    }

    Prometheus::Collector::Instance().shutdown();
    http_srv.reset();

    cout << "----- captured log lines: " << tee->size() << " -----" << endl;
    cout << "===== summary: " << g_failures << " failure(s) =====" << endl;
    return g_failures == 0 ? 0 : 1;
}
