/*
 * author: oaho
 * date: 2026/04/29
 * description: Integration test for the /metrics HTTP endpoint. Spins up a
 *              minimal HttpSession-based server in-process, registers the
 *              Prometheus handler, then probes the endpoint with HttpRequester
 *              and asserts content-type + key metric lines. Logs are tee'd
 *              to both console and an in-memory channel so failures show their
 *              context inline.
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "Common/config.h"
#include "Http/HttpRequester.h"
#include "Http/HttpSession.h"
#include "Network/TcpServer.h"
#include "Poller/EventPoller.h"
#include "Util/SSLBox.h"
#include "Util/logger.h"
#include "Util/mini.h"
#include "Util/onceToken.h"

#include "Prometheus/Collector.h"
#include "Prometheus/PrometheusHandler.h"

using namespace std;
using namespace toolkit;
using namespace mediakit;

namespace {

// ---------- 测试日志 channel: 同时打到 stdout 和内存 vector ----------
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

    vector<string> snapshot() const {
        std::lock_guard<std::mutex> lk(_mtx);
        return _lines;
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

bool contains(const string &haystack, const string &needle) {
    return haystack.find(needle) != string::npos;
}

// ---------- 同步 GET helper (HttpRequester 是异步的) ----------
struct HttpResult {
    bool got = false;
    string status;
    StrCaseMap headers;
    string body;
    string err;
};

HttpResult syncGet(const string &url, float timeout_sec = 5.0f) {
    auto result = make_shared<HttpResult>();
    auto sem = make_shared<semaphore>();
    auto requester = make_shared<HttpRequester>();
    requester->setMethod("GET");
    requester->startRequester(
        url,
        [result, sem, requester](const SockException &ex, const Parser &response) {
            if (ex) {
                result->err = ex.what();
            } else {
                result->got = true;
                result->status = response.Url();
                result->headers = response.getHeader();
                result->body = response.Content();
            }
            sem->post();
        },
        timeout_sec);
    sem->wait();
    return *result;
}

}  // namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // 日志: 控制台 + 内存截获 (内存截获的内容跟控制台一致, 失败时方便对照查看)
    auto tee = make_shared<TeeLogChannel>();
    Logger::Instance().add(tee);
    Logger::Instance().setWriter(make_shared<AsyncLogWriter>());

    // 选一个非特权端口避免冲突. 28100 跟项目 conventional 测试端口区段无冲突.
    constexpr uint16_t kPort = 28100;
    // 直接用字符串 key 写, 避免依赖 server/main.cpp 的私有 const char[] 声明
    mINI::Instance()["http.port"] = kPort;
    mINI::Instance()["http.sslport"] = 0;
    mINI::Instance()[Prometheus::kEnable] = 1;
    mINI::Instance()[Prometheus::kPath] = "/metrics";
    mINI::Instance()[Prometheus::kAuth] = 0;
    mINI::Instance()[Prometheus::kPushUrl] = "";
    // 测试不依赖外部 Pushgateway, 不启 PushGateway

    // 启动 HTTP server, 注册 Prometheus 模块
    TcpServer::Ptr http_srv(new TcpServer());
    http_srv->start<HttpSession>(kPort);

    Prometheus::Collector::Instance().init();
    Prometheus::PrometheusHandler::regist();

    // 等一个事件循环 tick 让监听器就绪
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    cout << "===== test 1: enable=1 auth=0, GET /metrics =====" << endl;
    {
        auto r = syncGet("http://127.0.0.1:" + to_string(kPort) + "/metrics");
        EXPECT(r.got, "request returns without socket error");
        EXPECT(r.status == "200", "status is 200, got=\"" + r.status + "\"");
        const string ct = r.headers["Content-Type"];
        EXPECT(contains(ct, "text/plain"), "content-type contains text/plain, got=\"" + ct + "\"");
        EXPECT(contains(ct, "version=0.0.4"), "content-type advertises Prometheus exposition v0.0.4");

        EXPECT(contains(r.body, "# HELP zlm_build_info"), "body has zlm_build_info HELP");
        EXPECT(contains(r.body, "# TYPE zlm_build_info gauge"), "body has zlm_build_info TYPE");
        EXPECT(contains(r.body, "zlm_build_info{"), "body has zlm_build_info series with labels");

        EXPECT(contains(r.body, "zlm_uptime_seconds"), "body has uptime metric");
        EXPECT(contains(r.body, "zlm_streams{schema=\"rtmp\"}"), "body has rtmp stream gauge");
        EXPECT(contains(r.body, "zlm_streams{schema=\"rtsp\"}"), "body has rtsp stream gauge");
        EXPECT(contains(r.body, "zlm_thread_load_percent{thread_id=\"0\",type=\"poller\"}"),
               "body has poller-0 load");

        // 简单合规性: 每行要么是空行, 要么 # HELP/# TYPE, 要么 metric_name+value
        size_t bad = 0;
        size_t total = 0;
        std::istringstream iss(r.body);
        string line;
        while (std::getline(iss, line)) {
            ++total;
            if (line.empty()) continue;
            if (line[0] == '#') continue;
            // metric line: 必须含至少一个空格 (分隔 metric_name 与 value)
            if (line.find(' ') == string::npos) {
                ++bad;
            }
        }
        EXPECT(bad == 0, "no malformed metric lines (bad=" + to_string(bad) + ", total=" + to_string(total) + ")");
        EXPECT(total > 20, "scrape returned at least 20 lines (got " + to_string(total) + ")");
    }

    cout << "===== test 2: 错误路径 GET /not-metrics 不应被 Prometheus 处理 =====" << endl;
    {
        auto r = syncGet("http://127.0.0.1:" + to_string(kPort) + "/not-metrics");
        EXPECT(r.got, "request returns");
        // 走文件服务器, 不存在 → 404 (具体行为是 ZLM 文件服务的)
        EXPECT(r.status == "404", "non-prometheus path falls through, status=" + r.status);
    }

    cout << "===== test 3: 模块二次 init 不崩 =====" << endl;
    {
        Prometheus::Collector::Instance().init();  // 应被 _initialized 守卫忽略
        EXPECT(Prometheus::Collector::Instance().initialized(), "still initialized");
    }

    Prometheus::Collector::Instance().shutdown();
    http_srv.reset();

    cout << "----- captured log lines: " << tee->snapshot().size() << " -----" << endl;
    cout << "===== summary: " << g_failures << " failure(s) =====" << endl;
    return g_failures == 0 ? 0 : 1;
}
