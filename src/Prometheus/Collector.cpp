/*
 * author: oaho
 * date: 2026/04/29
 * description: Implementation of the Collector. All scrape work is synchronous
 *              inside scrape(); callers (PrometheusHandler, PushGateway) are
 *              responsible for dispatching to a WorkThread to avoid blocking
 *              EventPoller. System metrics use platform branches; on Windows
 *              they are intentionally absent (rely on windows_exporter for
 *              host-level metrics).
 */

#include "Prometheus/Collector.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Util/logger.h"
#include "Util/mini.h"
#include "Util/onceToken.h"
#include "Util/util.h"
#include "Thread/TaskExecutor.h"
#include "Thread/WorkThreadPool.h"
#include "Poller/EventPoller.h"
#include "Network/Session.h"
#include "Network/Server.h"

#include "Common/MediaSource.h"

#include "Prometheus/Exporter.h"
#include "Prometheus/Registry.h"

#if defined(ENABLE_VERSION)
#include "version.h"
#endif

#if defined(__linux__)
#include <dirent.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <mach/task.h>
#include <unistd.h>
#endif

using namespace toolkit;
using std::string;

namespace mediakit {
namespace Prometheus {

// 配置 key 定义
const string kEnable           = "prometheus.enable";
const string kPath             = "prometheus.path";
const string kAuth             = "prometheus.auth";
const string kPushUrl          = "prometheus.push_url";
const string kPushIntervalSec  = "prometheus.push_interval_sec";
const string kPushJob          = "prometheus.push_job";
const string kPushInstance     = "prometheus.push_instance";

// 启动时把默认值写入 mINI. 与 WebHook.cpp 的 onceToken 注册套路一致.
static onceToken s_token([]() {
    mINI::Instance()[kEnable]          = 1;
    mINI::Instance()[kPath]            = "/metrics";
    mINI::Instance()[kAuth]            = 0;
    mINI::Instance()[kPushUrl]         = "";
    mINI::Instance()[kPushIntervalSec] = 15;
    mINI::Instance()[kPushJob]         = "zlmediakit";
    mINI::Instance()[kPushInstance]    = "";
});

namespace {

// 把 typeid 名字 demangle 后, 截掉命名空间前缀和 "Session" 后缀, 得到形如 "rtmp" / "rtsp" 的 type 标签
string sessionTypeFromTypeid(const Session &session) {
    string raw = demangle(typeid(session).name());
    auto pos = raw.find_last_of(':');
    if (pos != string::npos) {
        raw = raw.substr(pos + 1);
    }
    const string suffix = "Session";
    if (raw.size() > suffix.size() && raw.compare(raw.size() - suffix.size(), suffix.size(), suffix) == 0) {
        raw = raw.substr(0, raw.size() - suffix.size());
    }
    std::transform(raw.begin(), raw.end(), raw.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (raw.empty()) raw = "unknown";
    return raw;
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

// ---------------- Collector ----------------

Collector &Collector::Instance() {
    static Collector s_inst;
    return s_inst;
}

void Collector::init() {
    if (_initialized) return;

    auto &reg = Registry::Instance();
    _fam_build_info     = &reg.registerGauge("zlm_build_info", "ZLMediaKit build information (always 1)");
    _fam_uptime         = &reg.registerGauge("zlm_uptime_seconds", "Process uptime in seconds");
    _fam_cpu            = &reg.registerGauge("zlm_cpu_usage_percent", "Process CPU usage percent");
    _fam_mem_rss        = &reg.registerGauge("zlm_memory_rss_bytes", "Process resident set size in bytes");
    _fam_mem_virt       = &reg.registerGauge("zlm_memory_virtual_bytes", "Process virtual memory size in bytes");
    _fam_threads_total  = &reg.registerGauge("zlm_threads_total", "Total number of OS threads in this process");
    _fam_open_files     = &reg.registerGauge("zlm_open_files_total", "Number of open file descriptors");
    _fam_thread_load    = &reg.registerGauge("zlm_thread_load_percent", "Per-thread load percent (poller and work pools)");
    _fam_stream_total   = &reg.registerGauge("zlm_stream_total", "Number of currently registered media streams, by schema");
    _fam_stream_readers = &reg.registerGauge("zlm_stream_total_readers", "Total reader count across streams, by schema");
    _fam_session_total  = &reg.registerGauge("zlm_session_total", "Number of active sessions, by type");

    registerBuildInfo();

    _start_time = std::chrono::steady_clock::now();
    _initialized = true;

    InfoL << "prometheus collector initialized: " << reg.metricCount() << " metric families";
}

void Collector::shutdown() {
    if (!_initialized) return;
    Registry::Instance().clear();
    _initialized = false;
    InfoL << "prometheus collector shutdown";
}

void Collector::registerBuildInfo() {
#if defined(ENABLE_VERSION)
    Labels labels = {
        {"version", BUILD_TIME},
        {"branch",  BRANCH_NAME},
        {"commit",  COMMIT_HASH},
    };
#else
    Labels labels = {
        {"version", "unknown"},
        {"branch",  "unknown"},
        {"commit",  "unknown"},
    };
#endif
    _fam_build_info->withLabels(labels).set(1.0);
}

void Collector::collectUptime() {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - _start_time)
                    .count();
    _fam_uptime->withLabels({}).set(static_cast<double>(secs));
}

void Collector::collectThreadMetrics() {
    // 同 WebApi.cpp 的 getThreadsLoad / getWorkThreadsLoad: 直接读 executor 负载.
    auto poller_load  = EventPollerPool::Instance().getExecutorLoad();
    auto work_load    = WorkThreadPool::Instance().getExecutorLoad();

    for (size_t i = 0; i < poller_load.size(); ++i) {
        Labels lbl = {{"type", "poller"}, {"thread_id", std::to_string(i)}};
        _fam_thread_load->withLabels(lbl).set(poller_load[i]);
    }
    for (size_t i = 0; i < work_load.size(); ++i) {
        Labels lbl = {{"type", "work"}, {"thread_id", std::to_string(i)}};
        _fam_thread_load->withLabels(lbl).set(work_load[i]);
    }

    // 注意: 之前曾尝试在这里同步等 getExecutorDelay 回调以填充 zlm_thread_delay_ms,
    // 但 scrape 跑在 WorkThread 上, getExecutorDelay 又给每个 WorkThread (包括自己)
    // 派发任务等待全部完成 -> 自身阻塞导致死锁.
    // 暂从 v1 移除 delay 指标; 后续若需要, 应改为后台 timer 周期性刷新到缓存 gauge.
}

void Collector::collectBusinessMetrics() {
    // schema -> (stream_count, reader_count). 一次 for_each_media 遍历,
    // 累加进 map 后批量赋给 gauge. 已知 schema 即使为 0 也要上报 (避免在 grafana 上忽明忽暗).
    static const std::vector<string> kKnownSchemas = {"rtsp", "rtmp", "hls", "ts", "fmp4"};
    std::map<string, std::pair<int, int>> by_schema;
    for (const auto &s : kKnownSchemas) {
        by_schema[s] = {0, 0};
    }

    MediaSource::for_each_media([&by_schema](const MediaSource::Ptr &src) {
        const string &schema = src->getSchema();
        auto &slot = by_schema[schema];
        slot.first += 1;
        try {
            slot.second += src->totalReaderCount();
        } catch (...) {
            // 某些 source 在尚未就绪时会抛 NotImplemented, 视为 0
        }
    });

    for (const auto &kv : by_schema) {
        Labels lbl = {{"schema", kv.first}};
        _fam_stream_total->withLabels(lbl).set(kv.second.first);
        _fam_stream_readers->withLabels(lbl).set(kv.second.second);
    }

    // session 按 typeid 名分类
    std::map<string, int> by_type;
    SessionMap::Instance().for_each_session(
        [&by_type](const string & /*id*/, const Session::Ptr &session) {
            if (!session) return;
            by_type[sessionTypeFromTypeid(*session)] += 1;
        });
    for (const auto &kv : by_type) {
        Labels lbl = {{"type", kv.first}};
        _fam_session_total->withLabels(lbl).set(kv.second);
    }
}

// ----------- 系统指标平台分支 -----------

#if defined(__linux__)

static double readProcCpuPercent() {
    // /proc/self/stat 第 14/15 字段是 utime/stime (单位 jiffies). 取两次采样差值除以墙钟时间.
    static int64_t s_last_total_ms = 0;
    static int64_t s_last_wall_ms  = 0;

    FILE *fp = fopen("/proc/self/stat", "r");
    if (!fp) return 0.0;
    long unsigned utime = 0, stime = 0;
    long itime = 0, ctime = 0;
    // 跳过前 13 个字段
    int pid; char comm[256]; char state;
    int ppid, pgrp, sess, tty_nr, tpgid;
    unsigned flags;
    long unsigned minflt, cminflt, majflt, cmajflt;
    int rc = fscanf(fp,
        "%d %255s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld",
        &pid, comm, &state, &ppid, &pgrp, &sess, &tty_nr, &tpgid,
        &flags, &minflt, &cminflt, &majflt, &cmajflt,
        &utime, &stime, &itime, &ctime);
    fclose(fp);
    if (rc < 16) return 0.0;

    long ticks = sysconf(_SC_CLK_TCK);
    if (ticks <= 0) return 0.0;
    int64_t total_ms = (int64_t)((utime + stime) * 1000 / ticks);
    int64_t wall_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();

    double pct = 0.0;
    if (s_last_wall_ms > 0 && wall_ms > s_last_wall_ms) {
        pct = (total_ms - s_last_total_ms) * 100.0 / (wall_ms - s_last_wall_ms);
    }
    s_last_total_ms = total_ms;
    s_last_wall_ms  = wall_ms;
    return pct;
}

static void readProcMem(double &rss, double &vsz) {
    rss = 0.0;
    vsz = 0.0;
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            long kb = 0;
            sscanf(line + 6, " %ld", &kb);
            rss = (double)kb * 1024.0;
        } else if (strncmp(line, "VmSize:", 7) == 0) {
            long kb = 0;
            sscanf(line + 7, " %ld", &kb);
            vsz = (double)kb * 1024.0;
        }
    }
    fclose(fp);
}

static int readProcCount(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    int count = 0;
    while (struct dirent *e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        ++count;
    }
    closedir(d);
    return count;
}

void Collector::collectSystemMetrics() {
    _fam_cpu->withLabels({}).set(readProcCpuPercent());

    double rss = 0, vsz = 0;
    readProcMem(rss, vsz);
    _fam_mem_rss->withLabels({}).set(rss);
    _fam_mem_virt->withLabels({}).set(vsz);

    _fam_threads_total->withLabels({}).set(readProcCount("/proc/self/task"));
    _fam_open_files->withLabels({}).set(readProcCount("/proc/self/fd"));
}

#elif defined(__APPLE__)

static double readMacCpuPercent() {
    static int64_t s_last_total_us = 0;
    static int64_t s_last_wall_ms  = 0;

    task_thread_times_info_data_t thread_info_data{};
    mach_msg_type_number_t count = TASK_THREAD_TIMES_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_THREAD_TIMES_INFO,
                  (task_info_t)&thread_info_data, &count) != KERN_SUCCESS) {
        return 0.0;
    }
    task_basic_info_data_t basic_info_data{};
    count = TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_BASIC_INFO,
                  (task_info_t)&basic_info_data, &count) != KERN_SUCCESS) {
        return 0.0;
    }

    int64_t total_us =
        (int64_t)thread_info_data.user_time.seconds * 1000000 + thread_info_data.user_time.microseconds +
        (int64_t)thread_info_data.system_time.seconds * 1000000 + thread_info_data.system_time.microseconds +
        (int64_t)basic_info_data.user_time.seconds * 1000000 + basic_info_data.user_time.microseconds +
        (int64_t)basic_info_data.system_time.seconds * 1000000 + basic_info_data.system_time.microseconds;

    int64_t wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();

    double pct = 0.0;
    if (s_last_wall_ms > 0 && wall_ms > s_last_wall_ms) {
        pct = (total_us - s_last_total_us) / 1000.0 / (wall_ms - s_last_wall_ms) * 100.0;
    }
    s_last_total_us = total_us;
    s_last_wall_ms  = wall_ms;
    return pct;
}

static void readMacMem(double &rss, double &vsz) {
    rss = 0;
    vsz = 0;
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS) {
        rss = (double)info.resident_size;
        vsz = (double)info.virtual_size;
    }
}

static int readMacThreadsTotal() {
    proc_taskinfo info{};
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &info, sizeof(info)) <= 0) {
        return 0;
    }
    return info.pti_threadnum;
}

static int readMacFdCount() {
    int bytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (bytes <= 0) return 0;
    return bytes / (int)sizeof(proc_fdinfo);
}

void Collector::collectSystemMetrics() {
    _fam_cpu->withLabels({}).set(readMacCpuPercent());

    double rss = 0, vsz = 0;
    readMacMem(rss, vsz);
    _fam_mem_rss->withLabels({}).set(rss);
    _fam_mem_virt->withLabels({}).set(vsz);

    _fam_threads_total->withLabels({}).set(readMacThreadsTotal());
    _fam_open_files->withLabels({}).set(readMacFdCount());
}

#else  // Windows or other unsupported platforms

void Collector::collectSystemMetrics() {
    // 已在 spec 中决定: Windows 上不实现进程级指标, 由 windows_exporter 接管主机指标.
    // 业务指标仍然能正常工作. 启动后只 WarnL 一次, 避免日志爆炸.
    static onceToken s_warn([]() {
        WarnL << "prometheus: process-level system metrics are not implemented on this platform; "
                 "use windows_exporter (or equivalent) for host metrics";
    });
}

#endif

std::string Collector::scrape() {
    auto start = std::chrono::steady_clock::now();

    if (_initialized) {
        try {
            collectUptime();
            collectThreadMetrics();
            collectBusinessMetrics();
            collectSystemMetrics();
        } catch (const std::exception &e) {
            WarnL << "prometheus collect raised: " << e.what();
        }
    }

    auto body = Exporter::serialize(Registry::Instance());

    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    _last_scrape_ms.store(ms);
    _last_scrape_at_ms.store(nowMs());

    DebugL << "prometheus scrape: " << Registry::Instance().seriesCount() << " series, " << ms << "ms";
    return body;
}

void Collector::recordPushAttempt(bool success, int status_code, int64_t at_ms) {
    _last_push_at_ms.store(at_ms);
    _last_push_status.store(status_code);
    _total_push_attempts.fetch_add(1);
    if (!success) {
        _total_push_failures.fetch_add(1);
    }
}

}  // namespace Prometheus
}  // namespace mediakit
