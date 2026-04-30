/*
 * author: oaho
 * date: 2026/04/29
 * description: Implementation of the Prometheus registry primitives. CAS loop
 *              gives us double inc/dec on C++11 std::atomic<double>; Family
 *              stores series in a sorted std::map for stable iteration.
 */

#include "Prometheus/Registry.h"

#include <algorithm>
#include <stdexcept>

namespace mediakit {
namespace Prometheus {

// ---------------- Metric ----------------

Metric::Metric(MetricType type) : _type(type), _value(0.0) {}

void Metric::inc(double delta) {
    // CAS 循环模拟 fetch_add (atomic<double>::fetch_add 是 C++20 才有的).
    double cur = _value.load(std::memory_order_relaxed);
    while (!_value.compare_exchange_weak(cur, cur + delta,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
        // cur 已被 compare_exchange_weak 更新为最新值, 直接重试
    }
}

void Metric::dec(double delta) {
    inc(-delta);
}

// ---------------- Family ----------------

Family::Family(std::string name, std::string help, MetricType type)
    : _name(std::move(name)), _help(std::move(help)), _type(type) {}

Labels Family::canonicalize(const Labels &labels) {
    Labels copy = labels;
    std::sort(copy.begin(), copy.end(),
              [](const std::pair<std::string, std::string> &a,
                 const std::pair<std::string, std::string> &b) {
                  return a.first < b.first;
              });
    return copy;
}

Metric &Family::withLabels(const Labels &labels) {
    Labels key = canonicalize(labels);
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _series.find(key);
    if (it == _series.end()) {
        auto metric = std::make_shared<Metric>(_type);
        it = _series.emplace(std::move(key), metric).first;
    }
    return *it->second;
}

std::vector<std::pair<Labels, double>> Family::snapshot() const {
    std::vector<std::pair<Labels, double>> out;
    std::lock_guard<std::mutex> lock(_mutex);
    out.reserve(_series.size());
    for (const auto &kv : _series) {
        out.emplace_back(kv.first, kv.second->value());
    }
    return out;
}

size_t Family::seriesCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _series.size();
}

// ---------------- Registry ----------------

Registry &Registry::Instance() {
    static Registry s_instance;
    return s_instance;
}

Family &Registry::registerFamily(const std::string &name, const std::string &help, MetricType type) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _families.find(name);
    if (it != _families.end()) {
        // 重复注册视为编程错误: family 名字必须全局唯一.
        if (it->second->type() != type) {
            throw std::runtime_error("Prometheus metric '" + name + "' already registered with a different type");
        }
        return *it->second;
    }
    auto fam = std::make_shared<Family>(name, help, type);
    _families.emplace(name, fam);
    return *fam;
}

Family &Registry::registerCounter(const std::string &name, const std::string &help) {
    return registerFamily(name, help, MetricType::Counter);
}

Family &Registry::registerGauge(const std::string &name, const std::string &help) {
    return registerFamily(name, help, MetricType::Gauge);
}

std::vector<std::shared_ptr<Family>> Registry::families() const {
    std::vector<std::shared_ptr<Family>> out;
    std::lock_guard<std::mutex> lock(_mutex);
    out.reserve(_families.size());
    for (const auto &kv : _families) {
        out.push_back(kv.second);
    }
    return out;
}

size_t Registry::metricCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _families.size();
}

size_t Registry::seriesCount() const {
    size_t total = 0;
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto &kv : _families) {
        total += kv.second->seriesCount();
    }
    return total;
}

void Registry::clear() {
    std::lock_guard<std::mutex> lock(_mutex);
    _families.clear();
}

}  // namespace Prometheus
}  // namespace mediakit
