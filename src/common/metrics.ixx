module;

#include <chrono>
#include <cstdint>
#include <mutex>
#include <print>

export module common.metrics;

import common.key_array;

namespace tc = tskv::common;

namespace detail {

// clang-format off
using CounterKeysST = tc::key_set<
  "testc.foo_st"
>;

using CounterKeysMT = tc::key_set<
  "testc.foo_mt"
>;
using AdditiveGaugeKeysST = tc::key_set<
  "testg.foo_st"
>;

using AdditiveGaugeKeysMT = tc::key_set<
  "testg.bar_mt"
>;
// clang-format on

using CounterKeys       = tc::key_set_union_t<CounterKeysST, CounterKeysMT>;
using AdditiveGaugeKeys = tc::key_set_union_t<AdditiveGaugeKeysST, AdditiveGaugeKeysMT>;

template <tc::string_literal K>
consteval bool is_single_threaded_metric()
{
  return CounterKeysST::contains<K>() || AdditiveGaugeKeysST::contains<K>();
}

struct AdditiveGaugeShard {
  uint64_t current     = 0;
  uint64_t last_synced = 0;

  static void sync(std::uint64_t& global, const AdditiveGaugeShard& shard)
  {
    if (shard.current >= shard.last_synced) {
      const auto delta = shard.current - shard.last_synced;
      global += delta;
    }
    else {
      const auto delta = shard.last_synced - shard.last_synced;
      global -= delta;
    }
  }

  void post_sync() { this->last_synced = this->current; }
  void set(std::uint64_t n) { this->current = n; }
};

void sync_thread(std::chrono::steady_clock::duration min_interval);

struct ThreadLocalMetrics {
  tc::key_array<uint64_t, CounterKeysMT>                 counters{};
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
  tc::key_array<uint64_t, CounterKeys>       counters        = {};
  tc::key_array<uint64_t, AdditiveGaugeKeys> additive_gauges = {};

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
consteval bool is_multi_threaded_metric()
{
  return CounterKeysMT::contains<K>() || AdditiveGaugeKeysMT::contains<K>();
}

template <tc::string_literal K>
void add_counter(uint64_t n) noexcept
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
void set_gauge(uint64_t n) noexcept
{
  if constexpr (AdditiveGaugeKeysMT::contains<K>()) {
    local_metrics().additive_gauges.get<K>().set(n);
  }
  else if constexpr (AdditiveGaugeKeysST::contains<K>()) {
    global_metrics.additive_gauges.get<K>() = n;
  }
  else {
    static_assert(dependent_false<K>, "Unrecognized counter key.");
  }
}

} // namespace detail

export namespace tskv::common::metrics {

inline void sync_thread(std::chrono::steady_clock::duration min_interval = std::chrono::seconds{1})
{
  detail::sync_thread(min_interval);
}

template <tc::string_literal K>
inline void add_counter(uint64_t n) noexcept
{
  detail::add_counter<K>(n);
}

template <tc::string_literal K>
inline void inc_counter() noexcept
{
  add_counter<K>(1);
}

template <tc::string_literal K>
inline std::uint64_t get_counter() noexcept
{
  return detail::global_metrics.counters.get<K>();
}

template <tc::string_literal K>
inline void set_gauge(uint64_t n) noexcept
{
  detail::set_gauge<K>(n);
}

inline void global_reset()
{
  detail::global_metrics = detail::GlobalMetrics();
}

} // namespace tskv::common::metrics
