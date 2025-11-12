module;

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <format>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>

#include "tskv/common/attributes.hpp"

export module tskv.common.logging;

import tskv.common.enum_traits;

export namespace tskv::common {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Critical, Off };

template <>
struct enum_traits<LogLevel> {
  // clang-format off
  static constexpr std::array<std::pair<LogLevel, std::string_view>, 7> entries{{
    {LogLevel::Trace, "TRACE"},
    {LogLevel::Debug, "DEBUG"},
    {LogLevel::Info, "INFO"},
    {LogLevel::Warn, "WARN"},
    {LogLevel::Error, "ERROR"},
    {LogLevel::Critical, "CRITICAL"},
    {LogLevel::Off, "OFF"}
  }};
  // clang-format on
};

namespace detail {

inline std::atomic<LogLevel>& global_log_level() noexcept
{
  static std::atomic<LogLevel> lvl{LogLevel::Info};
  return lvl;
}

inline std::mutex& log_mutex() noexcept
{
  static std::mutex m;
  return m;
}

inline std::string format_timestamp() noexcept
{
  using clock           = std::chrono::system_clock;
  const auto        now = clock::now();
  const std::time_t t   = clock::to_time_t(now);

  std::tm tm_buf{};
  if (localtime_r(&t, &tm_buf) == nullptr) {
    return {};
  }

  char buf[32];
  if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf) == 0) {
    return {};
  }
  return std::string{buf};
}

inline bool is_enabled(LogLevel lvl) noexcept
{
  const auto current = global_log_level().load(std::memory_order_relaxed);
  return static_cast<std::uint8_t>(lvl) >= static_cast<std::uint8_t>(current);
}

inline void write_stderr(std::string_view line) noexcept
{
  std::lock_guard lock{log_mutex()};
  (void)std::fwrite(line.data(), 1, line.size(), stderr);
  (void)std::fflush(stderr);
}

} // namespace detail

// --------------------------------------------------------------------------
// Exported function definitions
// --------------------------------------------------------------------------

inline void set_log_level(LogLevel level) noexcept
{
  detail::global_log_level().store(level, std::memory_order_relaxed);
}

inline LogLevel get_log_level() noexcept
{
  return detail::global_log_level().load(std::memory_order_relaxed);
}

template <typename... Args>
void log(LogLevel level, std::source_location loc, std::string_view fmt, Args&&... args) noexcept
{
  if (!detail::is_enabled(level)) {
    return;
  }

  std::string message;
  try {
    message = std::vformat(fmt, std::make_format_args(std::forward<Args>(args)...));
  }
  catch (...) {
    // Fallback: just copy the raw format string.
    message.assign(fmt.begin(), fmt.end());
  }

  const std::string      ts        = detail::format_timestamp();
  const std::string_view level_str = to_string<LogLevel>(level);

  std::string line;
  try {
    line = std::format("[{}] [{}] {}:{} {}: {}\n",
      ts,
      level_str,
      loc.file_name(),
      loc.line(),
      loc.function_name(),
      message);
  }
  catch (...) {
    line = message;
    line.push_back('\n');
  }

  detail::write_stderr(line);
}

template <typename... Args>
[[noreturn]] TSKV_COLD_PATH void log_invariant_failure(
  std::source_location loc, char const* expr_str, std::string_view fmt, Args&&... args) noexcept
{
  // Build a message describing the invariant that failed.
  std::string user_msg;
  try {
    user_msg = std::vformat(fmt, std::make_format_args(std::forward<Args>(args)...));
  }
  catch (...) {
    user_msg.assign(fmt.begin(), fmt.end());
  }

  std::string combined;
  try {
    combined = std::format("INVARIANT VIOLATION: {}: {}", expr_str, user_msg);
  }
  catch (...) {
    combined = "INVARIANT VIOLATION";
  }

  // Reuse the normal logger; double-formatting is fine on this cold path.
  log(LogLevel::Critical, loc, "{}", combined);

  std::abort();
}

} // namespace tskv::common
