/*
 * author: oaho
 * date: 2026/04/29
 * description: Collector wires ZLMediaKit's existing data sources (EventPoller
 *              executor pool, MediaSource, SessionMap, OS sysinfo) into the
 *              Prometheus Registry. It is pull-based: scrape() reads everything
 *              fresh on demand, no hot-path patching required.
 */

#ifndef ZLMEDIAKIT_PROMETHEUS_COLLECTOR_H
#define ZLMEDIAKIT_PROMETHEUS_COLLECTOR_H

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

namespace mediakit {
namespace Prometheus {

// 配置 key 常量, 实际默认值在 Collector.cpp 的 onceToken 里通过 mINI 注册.
extern const std::string kEnable;
extern const std::string kPath;
extern const std::string kAuth;
extern const std::string kPushUrl;
extern const std::string kPushIntervalSec;
extern const std::string kPushJob;
extern const std::string kPushInstance;

class Family;

class Collector {
public:
    static Collector &Instance();

    // 注册所有指标 family + 写入 build_info / 启动时间. 启动时调用一次.
    void init();

    // 释放 Registry. 关闭时调用一次.
    void shutdown();

    // 触发一次完整采集 + 文本序列化. 这是同步阻塞调用 (~1ms 量级),
    // 调用方 (PrometheusHandler / PushGateway) 自行决定派到 WorkThread 还是 EventPoller.
    std::string scrape();

    // 调试入口: 上次 scrape 的耗时和时间戳. 给 getStatisticJson 看的.
    double lastScrapeDurationMs() const { return _last_scrape_ms.load(); }
    int64_t lastScrapeAtMs() const { return _last_scrape_at_ms.load(); }

    // Pushgateway 可见状态: 用于 getStatisticJson.
    void recordPushAttempt(bool success, int status_code, int64_t at_ms);
    int64_t lastPushAtMs() const { return _last_push_at_ms.load(); }
    int lastPushStatus() const { return _last_push_status.load(); }
    uint64_t totalPushAttempts() const { return _total_push_attempts.load(); }
    uint64_t totalPushFailures() const { return _total_push_failures.load(); }

    bool initialized() const { return _initialized; }

private:
    Collector() = default;
    Collector(const Collector &) = delete;
    Collector &operator=(const Collector &) = delete;

    // 各类采集动作, scrape() 内部依次调用.
    void collectUptime();
    void collectThreadMetrics();
    void collectBusinessMetrics();
    void collectSystemMetrics();

    // build_info 在 init() 写入一次, 后续不变.
    void registerBuildInfo();

    // family 句柄. 在 init() 阶段注册并缓存指针, 避免每次 scrape 都查 Registry.
    Family *_fam_build_info = nullptr;
    Family *_fam_uptime = nullptr;
    Family *_fam_cpu = nullptr;
    Family *_fam_mem_rss = nullptr;
    Family *_fam_mem_virt = nullptr;
    Family *_fam_threads_total = nullptr;
    Family *_fam_open_files = nullptr;
    Family *_fam_thread_load = nullptr;
    Family *_fam_thread_delay = nullptr;
    Family *_fam_stream_total = nullptr;
    Family *_fam_stream_readers = nullptr;
    Family *_fam_session_total = nullptr;

    bool _initialized = false;
    std::chrono::steady_clock::time_point _start_time;

    std::atomic<double> _last_scrape_ms{0.0};
    std::atomic<int64_t> _last_scrape_at_ms{0};

    std::atomic<int64_t> _last_push_at_ms{0};
    std::atomic<int> _last_push_status{0};
    std::atomic<uint64_t> _total_push_attempts{0};
    std::atomic<uint64_t> _total_push_failures{0};
};

}  // namespace Prometheus
}  // namespace mediakit

#endif  // ZLMEDIAKIT_PROMETHEUS_COLLECTOR_H
