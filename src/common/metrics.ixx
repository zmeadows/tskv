module;

//------------------------------------------------------------------------------
// Module: common.metrics
// Summary: performance-oriented server-side metrics
//
//  - metrics are mapped by compile-time-string to array-index
//    * e.g., add_counter<"net.bytes_written">(1024);
//  - separate single-threaded and multi-theaded metrics
//    * e.g., CounterKeysST vs. CounterKeysMT
//    * only multi-threaded metrics are synchronized (via flush_thread)
//    * all synchronization beyond flush_thread calls is hidden behind public API
//  - global_reset is not intended in actual use, only in tests
//  - counters and gauges are 64-bit unsigned, and currently allowed to freely wrap
//------------------------------------------------------------------------------

#include <chrono>
#include <cstdint>
#include <mutex>
#include <print>

#include "../common/macro_utils.hpp"

export module common.metrics;

import common.key_array;
namespace tc = tskv::common;

using namespace std::chrono_literals;

using counter_t = std::uint64_t;
using gauge_t   = std::uint64_t;

namespace detail {

using clock = std::chrono::steady_clock;

//==============================================================================
//  Counter
//==============================================================================

using CounterKeysST = tc::key_set<"testc.foo_st">;
using CounterKeysMT = tc::key_set<"testc.foo_mt">;
using CounterKeys   = tc::key_set_union_t<CounterKeysST, CounterKeysMT>;

//==============================================================================
//  AdditiveGauge
//==============================================================================

using AdditiveGaugeKeysST = tc::key_set<"testg.foo_st">;
using AdditiveGaugeKeysMT = tc::key_set<"testg.foo_mt">;
using AdditiveGaugeKeys   = tc::key_set_union_t<AdditiveGaugeKeysST, AdditiveGaugeKeysMT>;

struct AdditiveGaugeShard {
private:
  gauge_t current_     = 0;
  gauge_t last_synced_ = 0;

public:
  static void sync(gauge_t& global, const AdditiveGaugeShard& shard);

  FORCE_INLINE void    post_sync() { last_synced_ = current_; }
  FORCE_INLINE void    set(gauge_t val) { current_ = val; }
  FORCE_INLINE gauge_t current() { return current_; }
};

void AdditiveGaugeShard::sync(gauge_t& global, const AdditiveGaugeShard& shard)
{
  // global tracks the sum of last_synced_ per shard;
  // thread sync adjusts global by the difference between current_ and last_synced_.

  if (shard.current_ >= shard.last_synced_) {
    const auto delta = shard.current_ - shard.last_synced_;
    global += delta;
  }
  else {
    const auto delta = shard.last_synced_ - shard.current_;
    global -= delta;
  }
}

//==============================================================================
//  ThreadLocalMetrics
//==============================================================================

struct ThreadLocalMetrics {
  tc::key_array<counter_t, CounterKeysMT>                counters{};
  tc::key_array<AdditiveGaugeShard, AdditiveGaugeKeysMT> additive_gauges{};

  ThreadLocalMetrics() = default;

  ThreadLocalMetrics(const ThreadLocalMetrics&)            = delete;
  ThreadLocalMetrics(ThreadLocalMetrics&&)                 = delete;
  ThreadLocalMetrics& operator=(const ThreadLocalMetrics&) = delete;
  ThreadLocalMetrics& operator=(ThreadLocalMetrics&&)      = delete;

  void post_sync();

  ~ThreadLocalMetrics();
};

void ThreadLocalMetrics::post_sync()
{
  counters.data.fill(0);

  for (AdditiveGaugeShard& shard : additive_gauges.data) {
    shard.post_sync();
  }
}

inline ThreadLocalMetrics& local_metrics()
{
  thread_local ThreadLocalMetrics instance{};
  return instance;
}

//==============================================================================
//  GlobalMetrics
//==============================================================================

struct GlobalMetrics {
  tc::key_array<counter_t, CounterKeys>     counters{};
  tc::key_array<gauge_t, AdditiveGaugeKeys> additive_gauges{};

  void sync_with(const ThreadLocalMetrics& local);
};

constinit GlobalMetrics global_metrics{};
std::mutex              global_metrics_mutex;

void GlobalMetrics::sync_with(const ThreadLocalMetrics& local)
{
  this->counters += local.counters;

  tc::bitransform_key_arrays(
    this->additive_gauges, local.additive_gauges, AdditiveGaugeShard::sync);
}

void flush_thread(clock::duration min_interval)
{
  const auto now = clock::now();

  // Per-thread last-sync time
  thread_local clock::time_point last = now - min_interval;

  if (now - last < min_interval) {
    return;
  }
  last = now;

  ThreadLocalMetrics& local = local_metrics();

  {
    std::scoped_lock lock(global_metrics_mutex);
    global_metrics.sync_with(local);
  }

  local.post_sync();
}

ThreadLocalMetrics::~ThreadLocalMetrics()
{
  // If a thread terminates *without* calling flush_thread after their final updates,
  // this destructor ensures a final forced flush.
  flush_thread(0ms);
}

//==============================================================================
//  API IMPL
//==============================================================================

void print()
{
  std::scoped_lock lock(detail::global_metrics_mutex);

  const auto& metrics = detail::global_metrics;

  for (std::size_t i = 0; i < metrics.counters.size; i++) {
    std::println("{}: {}", metrics.counters.key_names[i], metrics.counters.data[i]);
  }

  for (std::size_t i = 0; i < metrics.additive_gauges.size; i++) {
    std::println("{}: {}", metrics.additive_gauges.key_names[i], metrics.additive_gauges.data[i]);
  }
}

template <auto>
inline constexpr bool dependent_false = false;

template <tc::string_literal K>
void add_counter(counter_t n) noexcept
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
void set_gauge(gauge_t n) noexcept
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

//==============================================================================
//  PUBLIC API
//==============================================================================

export namespace tskv::common::metrics {

using clock     = std::chrono::steady_clock;
using counter_t = counter_t;
using gauge_t   = gauge_t;

FORCE_INLINE void print()
{
  detail::print();
}

FORCE_INLINE void global_reset() // for testing purposes only!
{
  std::scoped_lock lock(detail::global_metrics_mutex);
  detail::global_metrics = detail::GlobalMetrics();
}

FORCE_INLINE void flush_thread(clock::duration min_interval = 1s)
{
  detail::flush_thread(min_interval);
}

template <tc::string_literal K>
FORCE_INLINE void add_counter(counter_t n) noexcept
{
  detail::add_counter<K>(n);
}

template <tc::string_literal K>
FORCE_INLINE void inc_counter() noexcept
{
  add_counter<K>(1);
}

template <tc::string_literal K>
FORCE_INLINE counter_t get_counter() noexcept
{
  std::scoped_lock lock(detail::global_metrics_mutex);
  return detail::global_metrics.counters.get<K>();
}

template <tc::string_literal K>
FORCE_INLINE void set_gauge(gauge_t n) noexcept
{
  detail::set_gauge<K>(n);
}

template <tc::string_literal K>
FORCE_INLINE gauge_t get_gauge() noexcept
{
  std::scoped_lock lock(detail::global_metrics_mutex);
  return detail::global_metrics.additive_gauges.get<K>();
}

} // namespace tskv::common::metrics
