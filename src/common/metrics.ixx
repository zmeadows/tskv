module;

#include <chrono>
#include <cstdint>
#include <mutex>
#include <print>

export module common.metrics;

import common.key_array;

namespace tc = tskv::common;

export namespace tskv::common::metrics {

using Counter = std::uint64_t;
using Gauge   = std::uint64_t;

} // namespace tskv::common::metrics

namespace detail {

using tskv::common::metrics::Counter;
using tskv::common::metrics::Gauge;

using CounterKeysST = tc::key_set<"testc.foo_st">;
using CounterKeysMT = tc::key_set<"testc.foo_mt">;
using CounterKeys   = tc::key_set_union_t<CounterKeysST, CounterKeysMT>;

using AdditiveGaugeKeysST = tc::key_set<"testg.foo_st">;
using AdditiveGaugeKeysMT = tc::key_set<"testg.foo_mt">;
using AdditiveGaugeKeys   = tc::key_set_union_t<AdditiveGaugeKeysST, AdditiveGaugeKeysMT>;

struct AdditiveGaugeShard {
private:
  Gauge current_     = 0;
  Gauge last_synced_ = 0;

public:
  static void sync(Gauge& global, const AdditiveGaugeShard& shard)
  {
    if (shard.current_ >= shard.last_synced_) {
      const auto delta = shard.current_ - shard.last_synced_;
      global += delta;
    }
    else {
      const auto delta = shard.last_synced_ - shard.current_;
      global -= delta;
    }
  }

  void  post_sync() { last_synced_ = current_; }
  void  set(Gauge val) { current_ = val; }
  Gauge current() { return current_; }
};

void sync_thread(std::chrono::steady_clock::duration min_interval);

struct ThreadLocalMetrics {
  tc::key_array<Counter, CounterKeysMT>                  counters{};
  tc::key_array<AdditiveGaugeShard, AdditiveGaugeKeysMT> additive_gauges{};

  ThreadLocalMetrics() = default;

  ThreadLocalMetrics(const ThreadLocalMetrics&)            = delete;
  ThreadLocalMetrics(ThreadLocalMetrics&&)                 = delete;
  ThreadLocalMetrics& operator=(const ThreadLocalMetrics&) = delete;
  ThreadLocalMetrics& operator=(ThreadLocalMetrics&&)      = delete;

  void post_sync()
  {
    counters = {}; // zero every counter

    for (AdditiveGaugeShard& shard : additive_gauges.data) {
      shard.post_sync();
    }
  }

  ~ThreadLocalMetrics()
  {
    sync_thread(std::chrono::milliseconds::zero());
    // eventually we will also have to clear per-thread state from AvgGauge, etc.
  }
};

inline ThreadLocalMetrics& local_metrics()
{
  thread_local ThreadLocalMetrics instance{};
  return instance;
}

struct GlobalMetrics {
  tc::key_array<Counter, CounterKeys>     counters        = {};
  tc::key_array<Gauge, AdditiveGaugeKeys> additive_gauges = {};

  void sync_with(const ThreadLocalMetrics& local)
  {
    this->counters += local.counters;

    tc::bitransform_key_arrays(
      this->additive_gauges, local.additive_gauges, AdditiveGaugeShard::sync);
  }

  void print() const
  {
    for (std::size_t i = 0; i < counters.size; i++) {
      std::println("{}: {}", counters.key_names[i], counters.data[i]);
    }
  }
};

constinit GlobalMetrics global_metrics{};

// TODO[@zmeadows][P0]: make this noexcept
void sync_thread(std::chrono::steady_clock::duration min_interval)
{
  using clock = std::chrono::steady_clock;

  const auto now = clock::now();

  // Per-thread last-sync time
  thread_local clock::time_point last = now - min_interval;

  if (now - last < min_interval) {
    return;
  }
  last = now;

  ThreadLocalMetrics& local = local_metrics();

  {
    static std::mutex global_metrics_mutex;
    std::scoped_lock  lock(global_metrics_mutex);
    global_metrics.sync_with(local);
  }

  local.post_sync();
}

template <auto>
inline constexpr bool dependent_false = false;

template <tc::string_literal K>
void add_counter(Counter n) noexcept
{
  if constexpr (CounterKeysMT::contains<K>()) {
    local_metrics().counters.get<K>() += n;
  }
  else if constexpr (CounterKeysST::contains<K>()) {
    global_metrics.counters.get<K>() += n;
  }
  else {
    static_assert(dependent_false<K>, "Unrecognized counter metric label.");
  }
}

template <tc::string_literal K>
void set_gauge(Gauge n) noexcept
{
  if constexpr (AdditiveGaugeKeysMT::contains<K>()) {
    local_metrics().additive_gauges.get<K>().set(n);
  }
  else if constexpr (AdditiveGaugeKeysST::contains<K>()) {
    global_metrics.additive_gauges.get<K>() = n;
  }
  else {
    static_assert(dependent_false<K>, "Unrecognized gauge key.");
  }
}

} // namespace detail

export namespace tskv::common::metrics {

inline void global_reset()
{
  detail::global_metrics = detail::GlobalMetrics();
}

inline void sync_thread(std::chrono::steady_clock::duration min_interval = std::chrono::seconds{1})
{
  detail::sync_thread(min_interval);
}

template <tc::string_literal K>
inline void add_counter(Counter n) noexcept
{
  detail::add_counter<K>(n);
}

template <tc::string_literal K>
inline void inc_counter() noexcept
{
  add_counter<K>(1);
}

template <tc::string_literal K>
inline Counter get_counter() noexcept
{
  return detail::global_metrics.counters.get<K>();
}

template <tc::string_literal K>
inline void set_gauge(Gauge n) noexcept
{
  detail::set_gauge<K>(n);
}

template <tc::string_literal K>
inline Gauge get_gauge() noexcept
{
  return detail::global_metrics.additive_gauges.get<K>();
}

} // namespace tskv::common::metrics
