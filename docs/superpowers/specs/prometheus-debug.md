# Prometheus Integration -- Operator Runbook

- **Date**: 2026-04-29
- **Audience**: ops / on-call engineers running ZLMediaKit MediaServer
- **Companion spec**: [`2026-04-29-prometheus-integration-design.md`](2026-04-29-prometheus-integration-design.md)

## 1. Configuration -- `[prometheus]`

Section in `release/<os>/<build>/config.ini` (or whichever ini you pass with `-c`):

```ini
[prometheus]
enable=1                  # 0 disables the whole module (no /metrics, no Pushgateway)
path=/metrics             # Endpoint path on the existing [http] port
auth=0                    # 0=open; 1=require ?secret=xxx (127.0.0.1 always bypassed)
push_url=                 # Pushgateway URL; empty disables push mode
push_interval_sec=15
push_job=zlmediakit
push_instance=            # Empty -> hostname()
```

Changes require a **service restart** -- v1 does not hot-reload `[prometheus]`.

## 2. Health checks

### 2a. Direct scrape
```sh
curl -sS http://<host>/metrics | head -40
```
Expected: `200 OK`, `Content-Type: text/plain; version=0.0.4; charset=utf-8`,
~38 series including `zlm_build_info`, `zlm_uptime_seconds`, `zlm_thread_load_percent{...}`,
`zlm_streams{schema="..."}`.

### 2b. JSON debug view
```sh
curl -sS http://<host>/index/api/getStatistic | jq '.data.Prometheus'
```
Returns the module's runtime status:
```json
{
  "enabled": true,
  "registry_metric_count": 11,
  "registry_series_count": 38,
  "last_scrape_duration_ms": 0.6,
  "last_scrape_at_ms": 1714463261499,
  "pushgateway": {
    "enabled": false,
    "last_push_at_ms": 0,
    "last_push_status": 0,
    "total_push_attempts": 0,
    "total_push_failures": 0
  }
}
```

### 2c. Format validation (after metric/label changes)
```sh
scripts/check_metrics.sh http://<host>/metrics
```
Wraps `curl` + `promtool check metrics`. Requires `promtool` in PATH
(`brew install prometheus`, or any Prometheus release tarball).

## 3. Local end-to-end stack

For development / dashboard iteration:

```sh
cd docker/dev
docker compose -f compose-prometheus.yml up --build
```

Then open:
- ZLMediaKit /metrics: <http://localhost:8088/metrics>
- Prometheus: <http://localhost:9090>
- Grafana (anonymous viewer): <http://localhost:3000> -> Dashboards -> ZLMediaKit -> ZLMediaKit Overview

The dashboard JSON lives in `docker/dev/grafana-dashboards/`; edit and refresh -- Grafana auto-reloads.

## 4. Logging conventions (in MediaServer log)

| Tag in log line | Level | Meaning |
|---|---|---|
| `prometheus collector initialized: N metric families` | `InfoL` | Module came up |
| `prometheus enabled, endpoint=/metrics, auth=0` | `InfoL` | Route registered |
| `pushgateway enabled: url=... job=... instance=... interval=Ns` | `InfoL` | Push timer armed |
| `pushgateway disabled (prometheus.push_url is empty)` | `InfoL` | Push mode is off by config |
| `prometheus scrape: N series, X.XXms` | `DebugL` | Each scrape (off by default; `-l 0` to see) |
| `pushgateway POST ok: url=..., status=200` | `DebugL` | Each successful push |
| `pushgateway POST failed: url=..., status=503, body=...` | `WarnL` | Each failed push (no backoff -- next tick continues on schedule) |
| `prometheus collector shutdown` | `InfoL` | Module released on service exit |

## 5. Troubleshooting decision tree

```
GET /metrics returns 404
 └─► Check `prometheus.enable=1` in the loaded config (release/<os>/<build>/config.ini).
     Confirm logs contain `prometheus enabled, endpoint=...` at startup.
     If running with `-c <other.ini>`, make sure THAT file has the [prometheus] section.

GET /metrics hangs / times out
 └─► Check the WorkThreadPool isn't saturated (other endpoints also slow?). Scrape work
     dispatches there. Compare `zlm_thread_load_percent{type="work"}` from a recent scrape.

GET /metrics returns 401
 └─► auth=1 is on. Pass `?secret=<api.secret>` (the value from the [api] section).
     127.0.0.1 should be auto-bypassed; if your client connects via ::1 (IPv6 loopback)
     auth IS enforced by design.

Prometheus says 'INVALID' in target health
 └─► Run scripts/check_metrics.sh against the same URL. promtool will name the offending
     line. Most often: a label value containing a control character that wasn't escaped --
     escalate as a Registry/Exporter bug.

Pushgateway not receiving pushes
 ├─ Logs say 'pushgateway disabled (prometheus.push_url is empty)' -> set push_url and restart.
 ├─ Logs say 'pushgateway POST failed: url=..., err=...' -> network/auth issue at the gateway side.
 │   The local module is healthy; fix the gateway (firewall, basic auth, TLS, route).
 └─ getStatistic shows total_push_attempts increasing but total_push_failures keeps pace ->
     push is firing but every reply is non-2xx. Check the gateway URL path (must be the
     base host:port; the module appends /metrics/job/<job>/instance/<instance> itself).
```

## 6. Known v1 limitations

- **No `zlm_thread_delay_ms`**. Removed because synchronously waiting for
  `WorkThreadPool::getExecutorDelay` from inside a WorkThread self-deadlocks.
  A future change can re-add it via a periodic background refresher gauge.
- **No hot-reload of `[prometheus]`**. SIGHUP reloads other sections; Prometheus
  config (path, auth, push_*) takes effect only after restart.
- **Windows process metrics not implemented**. CPU%, RSS, virtual mem,
  threads, FDs are absent from scrape on Windows. Business metrics
  (streams, sessions, thread load) work normally. Recommended pairing:
  deploy `windows_exporter` on the same host for system metrics.
