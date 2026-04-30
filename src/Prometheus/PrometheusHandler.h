/*
 * author: oaho
 * date: 2026/04/29
 * description: Registers the /metrics HTTP route on the existing WebApi server.
 *              The handler dispatches actual collection to WorkThreadPool, so
 *              the EventPoller thread holding the socket is freed immediately.
 */

#ifndef ZLMEDIAKIT_PROMETHEUS_HANDLER_H
#define ZLMEDIAKIT_PROMETHEUS_HANDLER_H

namespace mediakit {
namespace Prometheus {

class PrometheusHandler {
public:
    // 在 WebApi.cpp 的 installWebApi() 末尾调用一次. 路径与 auth 模式从 mINI 读取.
    static void regist();
};

}  // namespace Prometheus
}  // namespace mediakit

#endif  // ZLMEDIAKIT_PROMETHEUS_HANDLER_H
