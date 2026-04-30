/*
 * author: oaho
 * date: 2026/04/29
 * description: Optional periodic Pushgateway client. Idle when push_url is
 *              empty. Otherwise schedules itself on an EventPoller, dispatches
 *              collect+POST to a WorkThread, and records each attempt back
 *              into the Collector for the getStatisticJson debug view.
 */

#ifndef ZLMEDIAKIT_PROMETHEUS_PUSHGATEWAY_H
#define ZLMEDIAKIT_PROMETHEUS_PUSHGATEWAY_H

#include <atomic>
#include <memory>
#include <string>

namespace toolkit {
class EventPoller;
}

namespace mediakit {
namespace Prometheus {

class PushGateway {
public:
    static PushGateway &Instance();

    // 读取配置, 若 push_url 空则不启 timer; 否则按 push_interval_sec 周期推送.
    // 重复调用 start() 会先 stop() 再重启 (便于测试).
    void start();

    // 关闭 timer; 已发出去的 HTTP 请求由其自身回调收尾.
    void stop();

    bool running() const { return _running.load(); }

private:
    PushGateway() = default;
    PushGateway(const PushGateway &) = delete;
    PushGateway &operator=(const PushGateway &) = delete;

    void scheduleNext();
    void doPushOnce();

    std::atomic<bool> _running{false};
    int _interval_sec = 15;
    std::string _push_url;     // 不带 /metrics/job/... 后缀; 形如 http://gw:9091
    std::string _job;
    std::string _instance;     // 空时取 hostname
    std::shared_ptr<toolkit::EventPoller> _poller;
};

}  // namespace Prometheus
}  // namespace mediakit

#endif  // ZLMEDIAKIT_PROMETHEUS_PUSHGATEWAY_H
