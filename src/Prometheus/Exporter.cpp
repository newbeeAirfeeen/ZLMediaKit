/*
 * author: oaho
 * date: 2026/04/29
 * description: Implements the Prometheus text format serializer. Escaping rules
 *              follow the official spec: HELP escapes \ and \n; label values
 *              additionally escape ". Floats are formatted with the C locale to
 *              avoid platform-specific decimal separators.
 */

#include "Prometheus/Exporter.h"

#include <cmath>
#include <locale>
#include <sstream>

namespace mediakit {
namespace Prometheus {

namespace {

// HELP 行 escape: 反斜杠和换行
std::string escapeHelp(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out;
}

// label value escape: 反斜杠 / 换行 / 双引号
std::string escapeLabelValue(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '"') {
            out += "\\\"";
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out;
}

const char *typeName(MetricType t) {
    switch (t) {
        case MetricType::Counter:
            return "counter";
        case MetricType::Gauge:
            return "gauge";
    }
    return "untyped";
}

// 浮点格式化: 使用 C locale, 避免在 zh_CN 等本地化环境下输出 "1,5" 把 Prometheus 弄崩;
// NaN/Inf 走 Prometheus 规定的 "NaN" / "+Inf" / "-Inf" 字面量
std::string formatDouble(double v) {
    if (std::isnan(v)) {
        return "NaN";
    }
    if (std::isinf(v)) {
        return v > 0 ? "+Inf" : "-Inf";
    }
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << v;
    return oss.str();
}

}  // namespace

std::string Exporter::serialize(const Registry &registry) {
    std::ostringstream out;
    out.imbue(std::locale::classic());

    for (const auto &fam : registry.families()) {
        out << "# HELP " << fam->name() << ' ' << escapeHelp(fam->help()) << '\n';
        out << "# TYPE " << fam->name() << ' ' << typeName(fam->type()) << '\n';

        for (const auto &series : fam->snapshot()) {
            const Labels &labels = series.first;
            double value = series.second;
            out << fam->name();
            if (!labels.empty()) {
                out << '{';
                bool first = true;
                for (const auto &kv : labels) {
                    if (!first) {
                        out << ',';
                    }
                    first = false;
                    out << kv.first << "=\"" << escapeLabelValue(kv.second) << '"';
                }
                out << '}';
            }
            out << ' ' << formatDouble(value) << '\n';
        }
    }

    return out.str();
}

const char *Exporter::contentType() {
    return "text/plain; version=0.0.4; charset=utf-8";
}

}  // namespace Prometheus
}  // namespace mediakit
