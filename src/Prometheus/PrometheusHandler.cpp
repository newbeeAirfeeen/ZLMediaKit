/*
 * author: oaho
 * date: 2026/04/29
 * description: /metrics endpoint registered as a NoticeCenter listener on
 *              kBroadcastHttpRequest. This keeps src/ free of any reverse
 *              dependency on server/WebApi.h while still living on the existing
 *              HTTP server. The listener intercepts the configured path, sets
 *              consumed=true, and dispatches collection to WorkThreadPool.
 */

#include "Prometheus/PrometheusHandler.h"

#include "Util/logger.h"
#include "Util/mini.h"
#include "Util/NoticeCenter.h"
#include "Thread/WorkThreadPool.h"
#include "Http/HttpSession.h"
#include "Common/config.h"

#include "Prometheus/Collector.h"
#include "Prometheus/Exporter.h"

using namespace toolkit;
using std::string;

namespace mediakit {
namespace Prometheus {

namespace {
// 用静态对象的地址作为 NoticeCenter 监听器 tag, 与 ZLM 其他模块写法一致
static int s_listener_tag = 0;
}

void PrometheusHandler::regist() {
    if (!mINI::Instance()[kEnable].as<int>()) {
        return;
    }

    NoticeCenter::Instance().addListener(
        &s_listener_tag, Broadcast::kBroadcastHttpRequest,
        [](BroadcastHttpRequestArgs) {
            // 路径与鉴权设置都从 mINI 现读, 测试时改动 auth/path 即时生效, 主进程也允许 SIGHUP 重载.
            const string path = mINI::Instance()[kPath].as<string>();
            if (parser.Url() != path) {
                return;
            }
            consumed = true;

            // 鉴权: auth=1 时要求 ?secret=xxx 与 api.secret 一致;
            // 127.0.0.1 始终放行 (与 CHECK_SECRET 行为一致, 便于本机调试)
            const int auth = mINI::Instance()[kAuth].as<int>();
            if (auth && sender.get_peer_ip() != "127.0.0.1") {
                const string api_secret = mINI::Instance()["api.secret"].as<string>();
                string supplied;
                for (auto &kv : parser.getUrlArgs()) {
                    if (kv.first == "secret") {
                        supplied = kv.second;
                        break;
                    }
                }
                if (supplied.empty() || supplied != api_secret) {
                    HttpSession::KeyValue header;
                    header.emplace("Content-Type", "text/plain; charset=utf-8");
                    invoker(401, header, std::string("missing or invalid secret\n"));
                    return;
                }
            }

            // collect + serialize 派到 WorkThread, 释放 EventPoller
            WorkThreadPool::Instance().getExecutor()->async([invoker]() {
                string body;
                try {
                    body = Collector::Instance().scrape();
                } catch (const std::exception &e) {
                    WarnL << "prometheus scrape raised: " << e.what();
                    HttpSession::KeyValue h;
                    h.emplace("Content-Type", "text/plain; charset=utf-8");
                    invoker(500, h, std::string("collector failure\n"));
                    return;
                }
                HttpSession::KeyValue h;
                h.emplace("Content-Type", Exporter::contentType());
                invoker(200, h, body);
            });
        });

    InfoL << "prometheus enabled, endpoint=" << mINI::Instance()[kPath].as<string>()
          << ", auth=" << mINI::Instance()[kAuth].as<int>();
}

}  // namespace Prometheus
}  // namespace mediakit
