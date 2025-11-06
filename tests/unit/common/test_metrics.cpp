#include <doctest.h>

#include <barrier>
#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

import common.metrics;
namespace metrics = tskv::common::metrics;

TEST_SUITE("common.metrics")
{
  TEST_CASE("counters_single_threaded")
  {
    metrics::global_reset();

    CHECK(metrics::get_counter<"testc.foo_st">() == 0);
    metrics::inc_counter<"testc.foo_st">();
    CHECK(metrics::get_counter<"testc.foo_st">() == 1);
    metrics::inc_counter<"testc.foo_st">();
    CHECK(metrics::get_counter<"testc.foo_st">() == 2);
    metrics::add_counter<"testc.foo_st">(100);
    CHECK(metrics::get_counter<"testc.foo_st">() == 102);
  }

  TEST_CASE("counters_multi_threaded")
  {
    metrics::global_reset();
    constexpr std::size_t nthreads = 4;
    constexpr std::size_t niters   = 100000;

    // Start gate so all threads begin together
    std::barrier start_barrier(nthreads + 1);

    auto worker = [&] {
      start_barrier.arrive_and_wait();

      for (std::size_t i = 0; i < niters; ++i) {
        metrics::add_counter<"testc.foo_mt">(1);

        // force some sync/load on global_metrics, exercise the mutex
        if (i % 100 == 0) {
          metrics::sync_thread(0ms);
        }
      }

      // final flush for this thread
      metrics::sync_thread(0ms);
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
  }
}
