/*
 * author: oaho
 * date: 2026/04/29
 * description: PushGateway implementation. The timer lives on a dedicated
 *              EventPoller (so it runs even if all WorkThreads are saturated).
 *              Each tick: dispatch collect+POST to a WorkThread, then re-arm.
 *              Each POST attempt is recorded back into the Collector counters
 *              so getStatisticJson can show last_push_at / last_push_status /
 *              total_push_failures.
 */

#include "Prometheus/PushGateway.h"

#include <cerrno>
#include <chrono>
#include <unistd.h>

#include "Util/logger.h"
#include "Util/mini.h"
#include "Thread/WorkThreadPool.h"
#include "Poller/EventPoller.h"
#include "Http/HttpRequester.h"

#include "Prometheus/Collector.h"
#include "Prometheus/Exporter.h"
#include "Prometheus/Registry.h"

using namespace toolkit;
using std::string;

namespace mediakit {
namespace Prometheus {

namespace {

string defaultInstance() {
    char host[256] = {0};
    if (gethostname(host, sizeof(host) - 1) == 0) {
        return string(host);
    }
    return "unknown";
}

// 把 host:port (可能带尾随 /) 与 job/instance 拼成 Pushgateway URL.
//   <base>/metrics/job/<job>/instance/<instance>
string buildPushUrl(const string &base, const string &job, const string &instance) {
    string url = base;
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += "/metrics/job/";
    url += job;
    url += "/instance/";
    url += instance;
    return url;
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

PushGateway &PushGateway::Instance() {
    static PushGateway s_inst;
    return s_inst;
}

void PushGateway::start() {
    stop();

    _push_url = mINI::Instance()[kPushUrl].as<string>();
    if (_push_url.empty()) {
        InfoL << "pushgateway disabled (prometheus.push_url is empty)";
        return;
    }
    _interval_sec = mINI::Instance()[kPushIntervalSec].as<int>();
    if (_interval_sec <= 0) _interval_sec = 15;
    _job = mINI::Instance()[kPushJob].as<string>();
    if (_job.empty()) _job = "zlmediakit";
    _instance = mINI::Instance()[kPushInstance].as<string>();
    if (_instance.empty()) _instance = defaultInstance();

    _poller = EventPollerPool::Instance().getPoller();
    _running.store(true);
    InfoL << "pushgateway enabled: url=" << _push_url
          << " job=" << _job << " instance=" << _instance
          << " interval=" << _interval_sec << "s";

    scheduleNext();
}

void PushGateway::stop() {
    _running.store(false);
    // 已发出去的请求会按自己的 timeout 收尾; timer 重新检查 _running 时会自动结束链.
    _poller.reset();
}

void PushGateway::scheduleNext() {
    if (!_running.load() || !_poller) return;
    auto delay_ms = static_cast<uint64_t>(_interval_sec) * 1000ULL;
    auto self = this;
    _poller->doDelayTask(delay_ms, [self]() -> uint64_t {
        if (!self->_running.load()) return 0;
        self->doPushOnce();
        // 下次 tick 由 doPushOnce 完成后再排队 (保证 collect 完成前不会重叠)
        return 0;
    });
}

void PushGateway::doPushOnce() {
    auto self = this;
    WorkThreadPool::Instance().getExecutor()->async([self]() {
        if (!self->_running.load()) return;

        string body;
        try {
            body = Collector::Instance().scrape();
        } catch (const std::exception &e) {
            WarnL << "pushgateway: scrape raised " << e.what();
            self->scheduleNext();
            return;
        }

        const string url = buildPushUrl(self->_push_url, self->_job, self->_instance);
        auto requester = std::make_shared<HttpRequester>();
        requester->setMethod("POST");
        requester->addHeader("Content-Type", Exporter::contentType());
        requester->setBody(body);
        const int64_t at_ms = nowMs();

        requester->startRequester(
            url,
            [self, url, requester, at_ms](const SockException &ex, const Parser &response) {
                int status = 0;
                if (ex) {
                    WarnL << "pushgateway POST failed: url=" << url
                          << ", err=" << ex.what();
                    Collector::Instance().recordPushAttempt(false, status, at_ms);
                } else {
                    status = atoi(response.Url().c_str());
                    bool ok = (status >= 200 && status < 300);
                    if (ok) {
                        DebugL << "pushgateway POST ok: url=" << url
                               << ", status=" << status;
                    } else {
                        WarnL << "pushgateway POST failed: url=" << url
                              << ", status=" << status
                              << ", body=" << response.Content();
                    }
                    Collector::Instance().recordPushAttempt(ok, status, at_ms);
                }
                if (self->_running.load()) {
                    self->scheduleNext();
                }
            },
            std::min(static_cast<float>(self->_interval_sec), 10.0f));
    });
}

}  // namespace Prometheus
}  // namespace mediakit
