module;

//------------------------------------------------------------------------------
// Module: tskv.common.buffer
// Summary: simple fixed-capacity byte buffer with span-based API
//
//  - SimpleBuffer<BUFSIZE> owns a contiguous std::byte[BUFSIZE]
//    * capacity() is constexpr and fixed at compile time
//    * used_space()/free_space() expose current occupancy
//  - write/read provide copy-based producer/consumer operations
//    * write truncates to remaining free_space(), returns bytes written
//    * read truncates to available used_space(), returns bytes read
//  - writable_span/readable_span expose zero-copy views for staged I/O
//    * caller must pair writable_span with commit(n)
//    * caller must pair readable_span with consume(n)
//  - consume() compacts the buffer via memmove; no wrap-around or ring logic
//    * suitable as a simple baseline for higher-performance buffer variants
//  - type is trivially constructible and not thread-safe
//    * callers are responsible for any external synchronization
//------------------------------------------------------------------------------

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>

export module tskv.common.buffer;

export namespace tskv::common {

// TODO[@zmeadows][P3]: Implement higher performance alternatives with identical interface.
//                      (e.g., sliding window buffer, ring buffer, virtual memory mirrored ring buffer)

template <typename B>
concept Buffer =
  std::movable<B> &&
  requires(
    B& b, const B& cb, std::span<const std::byte> ssrc, std::span<std::byte> sdst, std::size_t n) {
    // 1) Basic capacity / occupancy API
    { B::capacity() } noexcept -> std::same_as<std::size_t>;
    { cb.used_space() } noexcept -> std::same_as<std::size_t>;
    { cb.free_space() } noexcept -> std::same_as<std::size_t>;
    { cb.empty() } noexcept -> std::same_as<bool>;
    { cb.full() } noexcept -> std::same_as<bool>;

    // 2) State management
    { b.clear() } noexcept -> std::same_as<void>;

    // 3) Copy-based producer/consumer operations
    { b.write_from(ssrc) } noexcept -> std::same_as<std::size_t>;
    { b.read_into(sdst) } noexcept -> std::same_as<std::size_t>;

    // 4) Zero-copy staged I/O
    { b.writable_span() } noexcept -> std::same_as<std::span<std::byte>>;
    { b.writable_span(n) } noexcept -> std::same_as<std::span<std::byte>>;
    { cb.readable_span() } noexcept -> std::same_as<std::span<const std::byte>>;
    { cb.readable_span(n) } noexcept -> std::same_as<std::span<const std::byte>>;

    { b.commit(n) } noexcept -> std::same_as<void>;
    { b.consume(n) } noexcept -> std::same_as<void>;
  };

template <std::size_t BUFSIZE>
struct SimpleBuffer {
private:
  std::byte   buf_[BUFSIZE]{};
  std::size_t offset_ = 0;

public:
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return BUFSIZE; }

  [[nodiscard]] std::size_t used_space() const noexcept { return offset_; }
  [[nodiscard]] std::size_t free_space() const noexcept { return BUFSIZE - offset_; }

  [[nodiscard]] bool empty() const noexcept { return offset_ == 0; }
  [[nodiscard]] bool full() const noexcept { return offset_ == BUFSIZE; }

  void clear() noexcept { offset_ = 0; }

  // Write as many bytes as will fit; returns bytes written.
  [[nodiscard]] std::size_t write_from(std::span<const std::byte> src) noexcept
  {
    const std::size_t count = std::min(src.size(), free_space());
    std::memcpy(buf_ + offset_, src.data(), count);
    offset_ += count;
    return count;
  }

  // Read as many bytes as available; returns bytes read.
  [[nodiscard]] std::size_t read_into(std::span<std::byte> dst) noexcept
  {
    const std::size_t count = std::min(dst.size(), used_space());
    std::memcpy(dst.data(), buf_, count);
    consume(count);
    return count;
  }

  // Returns a contiguous writable span up to `max_len` (and <= free_space()).
  // Caller must later call commit(n) with n <= returned span size.
  [[nodiscard]] std::span<std::byte> writable_span(std::size_t max_len = capacity()) noexcept
  {
    return std::span(buf_ + offset_, std::min(max_len, free_space()));
  }

  // Caller must ensure n <= returned span size from last writable_span call,
  // with exactly one commit call per writable_span call
  void commit(std::size_t n) noexcept
  {
    n = std::min(n, free_space());
    offset_ += n;
  }

  // Returns a contiguous readable span up to `max_len` (and <= used_space())
  // Caller can later optionally call consume(n) with n <= returned span size.
  [[nodiscard]] std::span<const std::byte> readable_span(
    std::size_t max_len = capacity()) const noexcept
  {
    return std::span(buf_, std::min(max_len, used_space()));
  }

  // Caller must ensure n <= returned span size from last readable_span call,
  // with exactly one consume call per readable_span call
  void consume(std::size_t n) noexcept
  {
    if (n == 0) {
      return;
    }

    n = std::min(n, used_space());

    const std::size_t leftover = offset_ - n;

    if (leftover > 0) {
      std::memmove(buf_, buf_ + n, leftover);
    }

    offset_ -= n;
  }
};

// Quick concept sanity check
static_assert(Buffer<SimpleBuffer<1024>>);

} // namespace tskv::common
