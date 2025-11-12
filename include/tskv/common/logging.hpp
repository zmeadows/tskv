// ============================================================================
// Convenience header for macros (e.g. include/tskv/logging.hpp)
// This is a *header*, not a module interface. It imports the module and
// defines the TSKV_* macros.
// ============================================================================
#pragma once

#include <source_location> // IWYU pragma: keep

// --------------------------------------------------------------------------
// Level constants for macros
// --------------------------------------------------------------------------

#define TSKV_LEVEL_TRACE ::tskv::common::LogLevel::Trace
#define TSKV_LEVEL_DEBUG ::tskv::common::LogLevel::Debug
#define TSKV_LEVEL_INFO ::tskv::common::LogLevel::Info
#define TSKV_LEVEL_WARN ::tskv::common::LogLevel::Warn
#define TSKV_LEVEL_ERROR ::tskv::common::LogLevel::Error
#define TSKV_LEVEL_CRITICAL ::tskv::common::LogLevel::Critical

// Compile-time active level (you can override this in your build if you want).
#ifndef TSKV_LOG_ACTIVE_LEVEL
#  ifdef NDEBUG
#    define TSKV_LOG_ACTIVE_LEVEL TSKV_LEVEL_INFO
#  else
#    define TSKV_LOG_ACTIVE_LEVEL TSKV_LEVEL_TRACE
#  endif
#endif

// --------------------------------------------------------------------------
// Core generic macro: TSKV_LOG(level_enum, fmt, ...)
// (You can later add compile-time stripping logic using TSKV_LOG_ACTIVE_LEVEL.)
// --------------------------------------------------------------------------

#define TSKV_LOG(level, fmt, ...)                                                                  \
  do {                                                                                             \
    ::tskv::common::log((level), std::source_location::current(), (fmt)__VA_OPT__(, __VA_ARGS__)); \
  } while (false)

// Level-specific convenience macros
#define TSKV_LOG_TRACE(fmt, ...) TSKV_LOG(TSKV_LEVEL_TRACE, fmt __VA_OPT__(, __VA_ARGS__))
#define TSKV_LOG_DEBUG(fmt, ...) TSKV_LOG(TSKV_LEVEL_DEBUG, fmt __VA_OPT__(, __VA_ARGS__))
#define TSKV_LOG_INFO(fmt, ...) TSKV_LOG(TSKV_LEVEL_INFO, fmt __VA_OPT__(, __VA_ARGS__))
#define TSKV_LOG_WARN(fmt, ...) TSKV_LOG(TSKV_LEVEL_WARN, fmt __VA_OPT__(, __VA_ARGS__))
#define TSKV_LOG_ERROR(fmt, ...) TSKV_LOG(TSKV_LEVEL_ERROR, fmt __VA_OPT__(, __VA_ARGS__))
#define TSKV_LOG_CRITICAL(fmt, ...) TSKV_LOG(TSKV_LEVEL_CRITICAL, fmt __VA_OPT__(, __VA_ARGS__))

// Runtime level setter convenience macro
#define TSKV_SET_LOG_LEVEL(level_name)                                                             \
  ::tskv::common::set_log_level(::tskv::common::LogLevel::level_name)

#define TSKV_INVARIANT(expr, fmt, ...)                                                             \
  do {                                                                                             \
    if (!(expr)) [[unlikely]] {                                                                    \
      ::tskv::common::log_invariant_failure(                                                       \
        std::source_location::current(), #expr, (fmt)__VA_OPT__(, __VA_ARGS__));                   \
    }                                                                                              \
  } while (false)
