# Prometheus Integration Design

- **Date**: 2026-04-29
- **Status**: Approved (pending implementation plan)
- **Author**: oaho

## 1. Goals & Non-Goals

### Goals
- Expose ZLMediaKit MediaServer runtime metrics in Prometheus exposition format.
- Cover process/system load (CPU, memory, threads, FDs, uptime) and aggregated business state (per-protocol stream counts, session counts, reader counts, per-thread load/delay).
- Provide both pull (`/metrics` endpoint) and optional push (Pushgateway) modes.
- Zero hot-path intrusion: all collection happens lazily at scrape time via existing read-only data sources.
- Cleanly excluded from SDK builds via a CMake option.

### Non-Goals
- Per-stream/per-session high-cardinality metrics (deferred — would explode series count).
- Network bytes counters (`*_bytes_total`) — deemed low value vs. cost.
- Histogram/Summary metric types — current metric set is gauge-only.
- Hot reload of `[prometheus]` configuration — restart required.
- Process-level system metrics on Windows — relies on `windows_exporter` instead.
- Unit tests for the Registry primitives and performance/concurrency tests — explicitly out of scope (covered by integration tests + `promtool` validation).

## 2. Architecture

```
src/
├── Prometheus/                              # New module, gated by ENABLE_PROMETHEUS
│   ├── Registry.h/.cpp                      # Counter / Gauge / Family / registry
│   ├── Exporter.h/.cpp                      # Prometheus text format serializer
│   ├── Collector.h/.cpp                     # Data-source adapters + collect orchestration
│   ├── PrometheusHandler.h/.cpp             # /metrics HTTP handler
│   └── PushGateway.h/.cpp                   # Optional Pushgateway client
├── Common/ ...
├── Http/ ...
└── ...

server/
├── main.cpp        ── #if defined(ENABLE_PROMETHEUS) → start Collector + PushGateway
├── WebApi.cpp      ── #if defined(ENABLE_PROMETHEUS) → register /metrics route
└── ...

CMakeLists.txt (top level)
└── option(ENABLE_PROMETHEUS "Enable Prometheus metrics" ${ENABLE_SERVER})
```

### Component responsibilities
- **Registry**: owns `Counter` / `Gauge` instances, indexes them by metric name + label set; thread-safe.
- **Collector**: on each `collect()` call, reads ZLMediaKit's existing data sources (EventPollerPool, MediaSource, SessionMap, OS sysinfo) and writes current values into the Registry.
- **Exporter**: serializes the Registry into Prometheus text format (`text/plain; version=0.0.4; charset=utf-8`).
- **PrometheusHandler**: HTTP handler mounted at the configured `path` on the existing HTTP API server.
- **PushGateway**: timer-driven optional client; periodically calls Collector + Exporter and `POST`s to a Pushgateway URL.

### Scrape data flow

```
Prometheus ──HTTP GET /metrics──► HttpSession (existing, EventPoller-N)
                                       │
                                       ▼
                           PrometheusHandler::onRequest      [EventPoller thread]
                                       │  dispatch async
                                       ▼
                           Collector::collect()              [WorkThread]
                                       │
                                       ▼
                           Exporter::serialize(Registry)     [WorkThread]
                                       │  callback
                                       ▼
                           HTTP 200 + text/plain body        [EventPoller thread]
```

`collect()` runs on a `WorkThreadPool` worker so the EventPoller is freed immediately. The pattern mirrors `WebApi.cpp`'s `API_ARGS_MAP_ASYNC` usage in `getThreadsLoad`.

### Code layout & build flag
- All Prometheus code lives under `src/Prometheus/`.
- Top-level CMake adds `option(ENABLE_PROMETHEUS "Enable Prometheus metrics" ${ENABLE_SERVER})`. SDK builds (Android, API SDK, CXX SDK) typically have `ENABLE_SERVER=OFF`, so Prometheus is auto-disabled.
- `src/CMakeLists.txt` conditionally adds the `Prometheus/` subdirectory to the source list when `ENABLE_PROMETHEUS=ON`.
- All call sites in `server/main.cpp` and `server/WebApi.cpp` are guarded by `#if defined(ENABLE_PROMETHEUS)` so the symbol is undefined and the include path is unused when the option is off.

## 3. Metric Set

All metrics are gauges. Naming convention: prefix `zlm_`, suffix with unit (`_bytes`, `_seconds`, `_percent`, `_ms`); counters (none in v1) would suffix with `_total`. Label values are lowercase, underscores instead of hyphens.

| Metric | Type | Labels | Data source | Notes |
|---|---|---|---|---|
| `zlm_build_info` | gauge=1 | `version`, `branch`, `commit` | `version.h` macros | Set once at startup |
| `zlm_uptime_seconds` | gauge | — | `steady_clock::now() - process_start` | Computed each scrape |
| `zlm_cpu_usage_percent` | gauge | — | Linux: `/proc/self/stat`; macOS: `task_info`; Windows: empty | Platform-abstracted |
| `zlm_memory_rss_bytes` | gauge | — | Linux: `/proc/self/status`; macOS: `mach_task_basic_info`; Windows: empty | |
| `zlm_memory_virtual_bytes` | gauge | — | Same | |
| `zlm_threads_total` | gauge | — | Linux: count `/proc/self/task`; macOS: `proc_pidinfo`; Windows: empty | |
| `zlm_open_files_total` | gauge | — | Linux: count `/proc/self/fd`; macOS: `proc_pidinfo`; Windows: empty | |
| `zlm_thread_load_percent` | gauge | `thread_id`, `type="poller\|work"` | `EventPollerPool::getExecutorLoad()` + `WorkThreadPool::getExecutorLoad()` | Same source as `getThreadsLoad`/`getWorkThreadsLoad` |
| `zlm_thread_delay_ms` | gauge | `thread_id`, `type="poller\|work"` | `EventPollerPool::getExecutorDelay()` + `WorkThreadPool::getExecutorDelay()` | Same |
| `zlm_stream_total` | gauge | `schema="rtsp\|rtmp\|hls\|ts\|fmp4"` | `MediaSource::for_each_media`, accumulated by schema | Single traversal fills all series |
| `zlm_stream_total_readers` | gauge | `schema=...` | Same traversal, sum of `getReaderCount()` | |
| `zlm_session_total` | gauge | `type="rtsp\|rtmp\|http\|..."` | `SessionMap::Instance().for_each_session`, classified by `getIdentifier` prefix | Single traversal fills all series |

**Total series estimate**: ~38 series (7 process-level + ~16 thread × 2 + 5 schema × 2 + ~5 session types). Scrape body: a few KB.

### Windows behavior
- Business metrics (stream/session/thread counts and loads) work normally — they use ZLToolKit cross-platform APIs.
- Process-level metrics (CPU%, RSS, virtual mem, FD count, thread total) are not implemented on Windows. The Collector's platform branch returns `empty` (the corresponding lines simply omit those metrics from the scrape body), and emits a one-time `WarnL` at startup naming the missing metrics.
- Recommended ops pattern on Windows: deploy `windows_exporter` alongside ZLMediaKit for host-level system metrics.

## 4. Configuration

New section in `conf/config.ini`:

```ini
[prometheus]
# Master switch. 0 disables module entirely (route not registered, no PushGateway).
enable=1

# /metrics endpoint path on the existing HTTP API server.
path=/metrics

# Auth mode for /metrics. 0 = open; 1 = require ?secret=xxx (matches WebApi convention).
auth=0

# Pushgateway URL. Empty string disables push mode. Example: http://pushgw:9091
push_url=

# Push period in seconds. Effective only when push_url is non-empty.
push_interval_sec=15

# Pushgateway job name.
push_job=zlmediakit

# Pushgateway instance label. Empty = use local hostname.
push_instance=
```

### Loading mechanism
- Defaults registered via `mINI` + `onceToken` in `src/Prometheus/Collector.cpp`, mirroring `WebHook.cpp`'s pattern.
- All config keys read once at startup. **No hot reload** — modifying `[prometheus]` requires service restart.

## 5. Concurrency Model

### Threading
- `PrometheusHandler::onRequest` runs on an EventPoller thread (the one owning the client socket).
- The handler immediately dispatches `Collector::collect()` + `Exporter::serialize()` to a `WorkThreadPool` worker via `WorkThreadPool::Instance().getExecutor()->async([](){...})`.
- The async callback writes the response back through the captured `HttpSession`, returning to the original EventPoller thread context.

### Registry safety
- `Counter` / `Gauge` store values in `std::atomic<double>`. Increment uses `compare_exchange_weak` loop (project is C++11; `std::atomic<double>::fetch_add` is C++20).
- `Family`'s label-set → metric instance map is guarded by `std::mutex` (no `shared_mutex` since the project targets C++11). Lookup is short and uncontended in practice — scrapes are infrequent.
- In v1 there is no business-thread write to gauges. The only writers are: (a) `Collector::init()` for one-time values like `zlm_build_info`, and (b) `Collector::collect()` for everything else. Both run on the WorkThread context. Atomic + mutex are defensive only.

### PushGateway timer
- One periodic task scheduled on an EventPoller via `EventPoller::doDelayTask` (recursive scheduling), which then dispatches the actual collect+POST work to a `WorkThreadPool` worker. The HTTP POST itself uses ZLToolKit's `HttpRequester` (async, non-blocking).

## 6. Lifecycle

### Startup (`server/main.cpp`)
```
1. mINI::Instance().parseFile(config.ini)             // existing
2. EventPollerPool::setPoolSize(N)                    // existing
3. #if defined(ENABLE_PROMETHEUS)
   if (mINI::Instance()["prometheus.enable"]) {
       Prometheus::Collector::Instance().init();      // register metric definitions
       Prometheus::PushGateway::Instance().start();   // no-op if push_url empty
   }
   #endif
4. TcpServer<HttpSession>::start(http_port)           // existing; routes already include /metrics
5. ... other services
```

### Route registration (`server/WebApi.cpp`)
At the end of `installWebApi()`:
```cpp
#if defined(ENABLE_PROMETHEUS)
if (mINI::Instance()["prometheus.enable"]) {
    Prometheus::PrometheusHandler::regist(api_regist);
}
#endif
```

### Shutdown (`server/main.cpp` signal handler)
```
1. #if defined(ENABLE_PROMETHEUS)
   if (enabled) {
       PushGateway::Instance().stop();   // cancel timer, await last POST
       Collector::Instance().shutdown(); // release Registry
   }
   #endif
2. ... other shutdowns
3. EventPollerPool::Instance().shutdown()
```

## 7. Error Handling

| Scenario | Behavior |
|---|---|
| `prometheus.enable=0` | Module not initialized; `/metrics` returns 404 (route not registered). |
| Collect throws (e.g. sysinfo syscall failure) | Caught at `Collector::collect()` boundary; offending metric omitted from body; remaining metrics serve normally; one `WarnL` per scrape at most. |
| `auth=1` with missing/wrong secret | HTTP 401, matching existing WebApi convention. |
| Pushgateway POST timeout / non-2xx | Single `WarnL` per failure including `url`, HTTP status code, and error message. **No backoff, no rate limiting** — every failure logs once, next push proceeds on schedule. |
| Pushgateway URL malformed at startup | One `ErrorL`; PushGateway not started; main service unaffected. |
| Oversized scrape body | No special handling — existing HTTP server buffer cap acts as natural ceiling. |

## 8. Testing

Two integration test executables, following the `tests/test_*.cpp` convention (one `.cpp` per executable, no GoogleTest, hand-rolled `assert` style consistent with `tests/test_httpApi.cpp`).

### `tests/test_prometheus_endpoint.cpp`
1. Start a minimal `TcpServer<HttpSession>` with default `[prometheus]` config (mirroring `tests/test_httpApi.cpp`).
2. Inject mock business state: register a few synthetic streams (rtmp×3, rtsp×2, hls×1) via `MediaSource` factories.
3. `HttpRequester` GET `http://127.0.0.1:<port>/metrics`.
4. Assert:
   - Status 200, `Content-Type: text/plain; version=0.0.4; charset=utf-8`.
   - Body contains `# HELP zlm_stream_total ...` and `# TYPE zlm_stream_total gauge`.
   - Body contains `zlm_stream_total{schema="rtmp"} 3` and `zlm_stream_total{schema="rtsp"} 2`.
   - Body contains a `zlm_build_info{...} 1` line.
   - Sysinfo metrics (CPU%, RSS) are present and parse as finite floats — no exact value assertion.
5. Restart server with `prometheus.enable=0`; assert GET `/metrics` returns 404 and `/index/api/getServerConfig` still works (verifies the switch does not pollute other routes).
6. With `auth=1`: missing secret → 401; correct secret → 200.

### `tests/test_prometheus_pushgateway.cpp`
All in-process; no external Pushgateway dependency.
1. Stand up an in-process mock Pushgateway: `TcpServer<HttpSession>` on a random port, handler records requests into `std::vector<MockRequest>`, response code configurable (200 / 503).
2. Configure `prometheus.push_url=http://127.0.0.1:<mock_port>`, `push_interval_sec=1`; start `PushGateway`.
3. Wait 3 seconds, assert:
   - Mock received ≥ 2 POSTs.
   - URL is `/metrics/job/zlmediakit/instance/<hostname>`.
   - Body matches `Collector::collect()` output captured at the same moment.
   - Content-Type is correct.
4. Reconfigure mock to return 503; wait 2 seconds; assert:
   - Pushes continue on schedule (no pause on failure).
   - Test logger captured `WarnL` lines containing `503` and the mock URL.
5. With `push_url=""`, start PushGateway, wait 3 seconds, assert mock received zero requests.

### Log capture in tests
Tests register a custom `LogChannel` whose `write()` (a) appends the formatted line to an in-memory `std::vector<std::string>` for assertions, and (b) also writes to `std::cout` so the developer running the test sees logs in real time.

### Out of scope (explicitly)
- Counter/Gauge unit tests.
- Text-format escape edge cases (delegated to `promtool check metrics` in the debug script).
- Performance / concurrency benchmarks.
- Windows platform tests.

## 9. Debug Tooling & Runbook

### 9.1 Local dev stack: `docker/dev/compose-prometheus.yml`
Docker Compose stack with `zlmediakit` + `prom/prometheus` + `grafana/grafana`, plus a pre-baked `prometheus.yml` (scrape target `zlmediakit:80`) and a Grafana dashboard JSON covering thread load, stream counts, and memory. Lives under `docker/dev/`, **not packaged into release artifacts**.

### 9.2 Format validation script: `scripts/check_metrics.sh`
```sh
#!/bin/sh
URL=${1:-http://localhost/metrics}
TMP=$(mktemp)
curl -sf "$URL" > "$TMP" || { echo "FAIL: scrape failed"; exit 1; }
promtool check metrics < "$TMP" || { echo "FAIL: invalid format"; exit 2; }
echo "OK: $(wc -l < "$TMP") lines, $(grep -c '^[a-z]' "$TMP") series"
```
Usable locally and from CI (when `promtool` is available). Compensates for skipping unit-level format tests.

### 9.3 In-server debug: extend `getStatisticJson`
When `ENABLE_PROMETHEUS=ON` and the module is enabled, `WebApi.cpp`'s `getStatisticJson` response gains a `Prometheus` field:
```json
{
  "Prometheus": {
    "enabled": true,
    "registry_metric_count": 12,
    "registry_series_count": 38,
    "last_scrape_duration_ms": 0.7,
    "last_scrape_at": "2026-04-29T10:42:11Z",
    "pushgateway": {
      "enabled": false,
      "last_push_at": null,
      "last_push_status": null,
      "total_push_attempts": 0,
      "total_push_failures": 0
    }
  }
}
```

### 9.4 Logging convention
| When | Level | Example |
|---|---|---|
| Startup | `InfoL` | `prometheus enabled, endpoint=/metrics, push_url=, auth=0` |
| Each scrape complete | `DebugL` | `prometheus scrape: 38 series, 0.7ms` |
| Pushgateway POST failure | `WarnL` | `pushgateway POST failed: url=..., status=503, err=...` |
| Pushgateway POST success | `DebugL` | `pushgateway POST ok: url=..., 234 bytes` |
| Module shutdown | `InfoL` | `prometheus shutdown` |

### 9.5 Runbook: `docs/superpowers/specs/prometheus-debug.md`
A separate operator-facing runbook covering:
- How to toggle `[prometheus]` settings.
- How to run `scripts/check_metrics.sh` for self-check.
- How to run `docker/dev/compose-prometheus.yml` for local end-to-end.
- How to read the Prometheus block in `getStatisticJson`.
- Troubleshooting decision tree:
  - `/metrics` returns 404 → check `enable`, check route registration log.
  - Prometheus cannot scrape → check `Content-Type`, firewall, `auth`.
  - Pushgateway not receiving → check `push_url`, look for `pushgateway POST failed` `WarnL`.

## 10. File Header Convention

All new C++ source/header files in `src/Prometheus/` follow the project convention (UTF-8 with BOM, English comments):

```cpp
/*
 * author: oaho
 * date: 2026/04/29
 * description: <one-line module purpose>
 */
```

## 11. Open Items Resolved During Brainstorming

| Decision | Outcome |
|---|---|
| Metric scope | System/process load + aggregated business state. **No** per-stream high-cardinality metrics. |
| Network bytes counters | **Dropped** — low value vs. cost. |
| Exposure mode | Pull (`/metrics` on existing HTTP port) **and** optional Push (Pushgateway). |
| Library choice | Hand-rolled (Option C): own Registry + Exporter, reuse ZLMediaKit's HTTP server and `HttpRequester` for transport. |
| Code location | `src/Prometheus/` (new top-level subdirectory). |
| SDK exclusion | CMake `option(ENABLE_PROMETHEUS ... ${ENABLE_SERVER})` + `#if defined(ENABLE_PROMETHEUS)` guards. |
| Collection strategy | Pull-on-collect / lazy. **No hot-path patching.** |
| Concurrency | `collect()` dispatched async to `WorkThreadPool`. |
| Hot reload | **Not in v1** — restart required. |
| Pushgateway failure handling | Single `WarnL` per failure with url + status; no backoff. |
| Windows process metrics | **Not implemented**; rely on `windows_exporter`. |
| Tests | Integration only (endpoint + Pushgateway); unit/perf tests **out of scope**. |
| Log capture in tests | Custom `LogChannel` appends to memory **and** writes to stdout. |
| Debug tooling | docker dev stack, `promtool` script, `getStatisticJson` extension, runbook. |
