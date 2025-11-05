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
using CounterKeysMT = tc::key_set<
  "net.foo"
>;

using CounterKeysST = tc::key_set<
  "net.bar"
>;
// clang-format on

using CounterKeys = tc::key_set_union_t<CounterKeysST, CounterKeysMT>;

template <tc::string_literal K>
consteval bool is_multi_threaded_metric()
{
  return CounterKeysMT::contains<K>();
}

template <tc::string_literal K>
consteval bool is_single_threaded_metric()
{
  return CounterKeysST::contains<K>();
}

struct ThreadLocalMetrics {
  tc::key_array<uint64_t, CounterKeysMT> counters;

  void reset() { counters = {}; }
};

inline ThreadLocalMetrics& local_metrics()
{
  thread_local ThreadLocalMetrics instance{};
  return instance;
}

struct GlobalMetrics {
  tc::key_array<uint64_t, CounterKeys> counters = {};

  void sync(const ThreadLocalMetrics& local) { this->counters += local.counters; }

  void print() const
  {
    for (std::size_t i = 0; i < counters.size; i++) {
      std::println("{}: {}", counters.key_names[i], counters.data[i]);
    }
  }
};

constinit GlobalMetrics global_metrics{};

void sync_thread(std::chrono::steady_clock::duration min_interval)
{
  using clock = std::chrono::steady_clock;

  // Per-thread last-sync time
  thread_local clock::time_point last{};
  thread_local bool              initialized = false;

  const auto now = clock::now();
  if (initialized && now - last < min_interval) {
    return;
  }
  initialized = true;
  last        = now;

  ThreadLocalMetrics& local = local_metrics();

  static std::mutex global_metrics_mutex;
  {
    std::scoped_lock lock(global_metrics_mutex);
    global_metrics.sync(local);
  }

  local.reset();
}

template <auto>
inline constexpr bool dependent_false = false;

template <tc::string_literal K>
void add_counter(uint64_t n) noexcept
{
  if constexpr (is_multi_threaded_metric<K>()) {
    local_metrics().counters.get<K>() += n;
  }
  else if constexpr (is_single_threaded_metric<K>()) {
    global_metrics.counters.get<K>() += n;
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
} // namespace tskv::common::metrics
