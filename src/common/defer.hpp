// clang-format off
// NOLINTBEGIN

// defer.hpp — tiny C++23 "defer" built on scope_exit
// Usage:
//   #include "defer.hpp"
//   void f() {
//     FILE* fp = std::fopen("x.txt", "w");
//     if (!fp) return;
//     defer { std::fclose(fp); }; // runs on scope exit
//   }
//
// Notes:
// - Captures by reference ([&]) intentionally; adjust inside the block if needed.
// - The deferred block must not throw; scope_exit's destructor is noexcept.

#pragma once
#include <type_traits>
#include <utility>

#if __has_include(<scope>)
  #include <scope>
  namespace defer_detail { namespace se = std; }
#elif __has_include(<experimental/scope>)
  #include <experimental/scope>
  namespace defer_detail { namespace se = std::experimental; }
#else
  #error "No <scope> or <experimental/scope> available for scope_exit."
#endif

namespace defer_detail {

// Helper that lets us write: defer { /*...*/ };
struct factory {
  template <class F>
  constexpr auto operator+(F&& f) const
    noexcept(noexcept(se::scope_exit<std::decay_t<F>>{std::forward<F>(f)}))
  {
    return se::scope_exit<std::decay_t<F>>{std::forward<F>(f)};
  }
};

} // namespace defer_detail

// Internal unique-name generator
#define _defer_CONCAT_IMPL(x, y) x##y
#define _defer_CONCAT(x, y) _defer_CONCAT_IMPL(x, y)

// The "defer" one-liner: `defer { /* cleanup */ };`
#if defined(__COUNTER__)
  #define defer \
    [[maybe_unused]] auto _defer_CONCAT(_defer_guard_, __COUNTER__) = ::defer_detail::factory{} + [&]()
#else
  // Fallback if __COUNTER__ is unavailable
  #define defer \
    [[maybe_unused]] auto _defer_CONCAT(_defer_guard_, __LINE__) = ::defer_detail::factory{} + [&]()
#endif

// NOLINTEND
// clang-format on
