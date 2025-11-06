#include <doctest.h>

#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

import common.metrics;
namespace metrics = tskv::common::metrics;

TEST_SUITE("common.metrics")
{
  TEST_CASE("counters.single_threaded")
  {
    metrics::global_reset();

    CHECK(metrics::get_counter<"testc.foo_st">() == 0);
    metrics::inc_counter<"testc.foo_st">();
    CHECK(metrics::get_counter<"testc.foo_st">() == 1);
    metrics::inc_counter<"testc.foo_st">();
    CHECK(metrics::get_counter<"testc.foo_st">() == 2);
    metrics::add_counter<"testc.foo_st">(100);
    CHECK(metrics::get_counter<"testc.foo_st">() == 102);

    metrics::global_reset();
    CHECK(metrics::get_counter<"testc.foo_st">() == 0);
  }

  TEST_CASE("counters.multi_threaded")
  {
    metrics::global_reset();

    constexpr std::size_t nthreads = 4;
    constexpr std::size_t niters   = 100000;

    // Start gate so all threads begin together
    std::barrier start_barrier(nthreads + 1);

    auto worker = [&] {
      start_barrier.arrive_and_wait();

      for (metrics::counter_t i = 0; i < niters; ++i) {
        metrics::add_counter<"testc.foo_mt">(1);

        // force some sync/load on global_metrics, exercise the mutex
        if (i % 10 == 0) {
          metrics::flush_thread(0ms);
        }
      }

      // final flush for this thread
      metrics::flush_thread(0ms);
    };

    std::vector<std::jthread> threads;
    threads.reserve(nthreads);
    for (std::size_t i = 0; i < nthreads; ++i) {
      threads.emplace_back(worker);
    }

    worker();

    for (auto& t : threads) {
      t.join();
    }

    CHECK(metrics::get_counter<"testc.foo_mt">() == (nthreads + 1) * niters);

    metrics::global_reset();
    CHECK(metrics::get_counter<"testc.foo_mt">() == 0);
  }

  TEST_CASE("additive_gauges.single_threaded")
  {
    metrics::global_reset();

    CHECK(metrics::get_gauge<"testg.foo_st">() == 0);

    const auto gauge_values = {3, 5, 4, 1000, 99, 123, 100, 105, 9999, 5000};

    for (metrics::gauge_t g : gauge_values) {
      metrics::set_gauge<"testg.foo_st">(g);
      CHECK(metrics::get_gauge<"testg.foo_st">() == g);
    }

    metrics::global_reset();
    CHECK(metrics::get_gauge<"testg.foo_st">() == 0);
  }

  TEST_CASE("additive_gauges.multi_threaded")
  {
    metrics::global_reset();

    std::array<metrics::gauge_t, 100000> ran_gauge_values;
    {
      std::mt19937_64 rng(0xDEADBEEF); // NOLINT

      constexpr metrics::gauge_t MAX_U64 = std::numeric_limits<metrics::gauge_t>::max();

      std::uniform_int_distribution<metrics::gauge_t> dist(0, MAX_U64);
      std::ranges::generate(ran_gauge_values, [&] { return dist(rng); });

      ran_gauge_values.front() = 0;
      ran_gauge_values.back()  = MAX_U64;
    }

    constexpr std::size_t nthreads = 4;
    std::barrier          start_barrier(nthreads + 1);

    constexpr metrics::gauge_t final_gauge_val = 123;

    auto worker = [&] {
      start_barrier.arrive_and_wait();

      size_t i = 0;
      for (const metrics::gauge_t n : ran_gauge_values) {
        metrics::set_gauge<"testg.foo_mt">(n);

        if (i++ % 10 == 0) {
          metrics::flush_thread(0ms);
        }
      }

      metrics::set_gauge<"testg.foo_mt">(final_gauge_val);
      metrics::flush_thread(0ms);
    };

    std::vector<std::jthread> threads;
    threads.reserve(nthreads);
    for (std::size_t i = 0; i < nthreads; ++i) {
      threads.emplace_back(worker);
    }

    worker();

    for (auto& t : threads) {
      t.join();
    }

    CHECK(metrics::get_gauge<"testg.foo_mt">() == (nthreads + 1) * final_gauge_val);

    metrics::global_reset();
    CHECK(metrics::get_gauge<"testg.foo_mt">() == 0);
  }
}
