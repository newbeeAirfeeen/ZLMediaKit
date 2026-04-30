/*
 * author: oaho
 * date: 2026/04/29
 * description: Prometheus metric registry primitives (Counter, Gauge, Family,
 *              Registry). Thread-safe via std::atomic<double> values and a
 *              std::mutex on the per-Family series index. C++11 compatible.
 */

#ifndef ZLMEDIAKIT_PROMETHEUS_REGISTRY_H
#define ZLMEDIAKIT_PROMETHEUS_REGISTRY_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mediakit {
namespace Prometheus {

enum class MetricType {
    Counter,
    Gauge,
};

// Label set is an ordered list of (name, value) pairs. Order matters for both
// hashing/lookup and exposition output, so the Family always sorts on insert.
using Labels = std::vector<std::pair<std::string, std::string>>;

// 单个 metric series 的当前值. 内部使用 std::atomic<double>; 由于 C++11 的
// std::atomic<double> 没有 fetch_add, inc/dec 走 compare_exchange_weak 循环.
class Metric {
public:
    explicit Metric(MetricType type);

    MetricType type() const { return _type; }

    double value() const { return _value.load(std::memory_order_relaxed); }
    void set(double v) { _value.store(v, std::memory_order_relaxed); }
    void inc(double delta = 1.0);
    void dec(double delta = 1.0);

private:
    MetricType _type;
    std::atomic<double> _value;
};

// Family 把同名指标的所有 series (按 label set 区分) 聚在一起.
class Family {
public:
    Family(std::string name, std::string help, MetricType type);

    const std::string &name() const { return _name; }
    const std::string &help() const { return _help; }
    MetricType type() const { return _type; }

    // 取一个 (label set) 对应的 series; 不存在则创建.
    Metric &withLabels(const Labels &labels);

    // 给序列化器读取的快照. 返回的 (Labels, value) 列表是稳定排序后的副本,
    // 这样输出顺序可预测, 测试也好断言.
    std::vector<std::pair<Labels, double>> snapshot() const;

    // 当前 series 数量 (用于 getStatisticJson 的统计字段).
    size_t seriesCount() const;

private:
    static Labels canonicalize(const Labels &labels);

    std::string _name;
    std::string _help;
    MetricType _type;
    mutable std::mutex _mutex;
    std::map<Labels, std::shared_ptr<Metric>> _series;
};

// 进程级单例. 注册阶段拿 Family, 后续 Family.withLabels(...) 即可获得 series.
class Registry {
public:
    static Registry &Instance();

    Family &registerCounter(const std::string &name, const std::string &help);
    Family &registerGauge(const std::string &name, const std::string &help);

    // 序列化器迭代用. 返回顺序按指标名字典序, 便于输出稳定.
    std::vector<std::shared_ptr<Family>> families() const;

    // 当前注册的 family / series 总量, 调试入口使用.
    size_t metricCount() const;
    size_t seriesCount() const;

    // 清空所有 family. 仅 shutdown 路径调用.
    void clear();

private:
    Registry() = default;
    Registry(const Registry &) = delete;
    Registry &operator=(const Registry &) = delete;

    Family &registerFamily(const std::string &name, const std::string &help, MetricType type);

    mutable std::mutex _mutex;
    std::map<std::string, std::shared_ptr<Family>> _families;
};

}  // namespace Prometheus
}  // namespace mediakit

#endif  // ZLMEDIAKIT_PROMETHEUS_REGISTRY_H
