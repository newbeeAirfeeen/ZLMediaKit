/*
 * author: oaho
 * date: 2026/04/29
 * description: Serialize a Prometheus Registry into the text exposition format
 *              (version 0.0.4). This is the only output format we support, and
 *              it is also what the Pushgateway client posts to the gateway.
 */

#ifndef ZLMEDIAKIT_PROMETHEUS_EXPORTER_H
#define ZLMEDIAKIT_PROMETHEUS_EXPORTER_H

#include <string>

#include "Prometheus/Registry.h"

namespace mediakit {
namespace Prometheus {

class Exporter {
public:
    // 序列化整张 Registry 为符合 Prometheus 规范的文本.
    // 返回的字符串以 '\n' 结尾, 可直接作为 HTTP 响应体或 Pushgateway POST body.
    static std::string serialize(const Registry &registry);

    // /metrics 端点和 Pushgateway POST 都用这个 Content-Type.
    static const char *contentType();
};

}  // namespace Prometheus
}  // namespace mediakit

#endif  // ZLMEDIAKIT_PROMETHEUS_EXPORTER_H
