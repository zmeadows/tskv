#include <cstddef>
#include <cstring>
#include <doctest.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import common.buffer;
namespace tc = tskv::common;

namespace { // helper functions

// Convert a string_view to a span of bytes (ASCII-only tests, so this is fine).
inline std::span<const std::byte> as_bytes(std::string_view s)
{
  return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

// Write a whole string into the buffer; returns bytes written.
template <std::size_t N>
std::size_t write_string(tc::SimpleBuffer<N>& buf, std::string_view s)
{
  return buf.write(as_bytes(s));
}

// Read up to max_len bytes from the buffer into a std::string.
template <std::size_t N>
std::string read_string(tc::SimpleBuffer<N>& buf, std::size_t max_len = N)
{
  std::vector<std::byte> tmp(max_len);
  std::span<std::byte>   dst(tmp.data(), tmp.size());
  const auto             n = buf.read(dst);
  return std::string(reinterpret_cast<const char*>(tmp.data()), n);
}

std::string read_string(std::span<const std::byte> bytes)
{
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// Non-consuming peek via readable_span.
template <std::size_t N>
std::string peek_string(const tc::SimpleBuffer<N>& buf, std::size_t max_len)
{
  auto src = buf.readable_span(max_len);
  return std::string(reinterpret_cast<const char*>(src.data()), src.size());
}

} // namespace

TEST_SUITE("common.buffer")
{
  TEST_CASE("default_state")
  {
    tc::SimpleBuffer<8> buf;

    CHECK(buf.capacity() == 8);
    CHECK(buf.used_space() == 0);
    CHECK(buf.free_space() == 8);
    CHECK(buf.empty());
    CHECK_FALSE(buf.full());
  }

  TEST_CASE("write_read_roundtrip")
  {
    tc::SimpleBuffer<16> buf;

    const std::string_view input = "hello";

    const auto written = write_string(buf, input);
    CHECK(written == input.size());
    CHECK(buf.used_space() == written);
    CHECK(buf.free_space() == buf.capacity() - written);
    CHECK_FALSE(buf.empty());

    const auto output = read_string(buf);
    CHECK(output == input);
    CHECK(buf.empty());
    CHECK(buf.used_space() == 0);
    CHECK(buf.free_space() == buf.capacity());
  }

  TEST_CASE("write_truncates_on_overflow")
  {
    tc::SimpleBuffer<8> buf;

    const std::string_view input = "ABCDEFGHIJK"; // 11 bytes

    const auto written = write_string(buf, input);
    CHECK(written == buf.capacity());
    CHECK(buf.full());
    CHECK(buf.used_space() == buf.capacity());
    CHECK(buf.free_space() == 0);

    // Further writes should not fit.
    const auto extra_written = write_string(buf, "Z");
    CHECK(extra_written == 0);

    const auto output = read_string(buf);
    CHECK(output == std::string("ABCDEFGH")); // first 8 bytes only
  }

  TEST_CASE("multiple_writes_and_reads")
  {
    tc::SimpleBuffer<8> buf;

    write_string(buf, "abc");
    write_string(buf, "def");
    CHECK(buf.used_space() == 6);
    CHECK(peek_string(buf, 6) == "abcdef");

    const auto first = read_string(buf, 4);
    CHECK(first == "abcd");
    CHECK(buf.used_space() == 2);
    CHECK(peek_string(buf, 4) == "ef");

    // After a read, data should have been memmoved to the front.
    write_string(buf, "ghij"); // 2 existing + 4 new = 6 bytes
    CHECK(buf.used_space() == 6);
    CHECK(peek_string(buf, 6) == "efghij");
  }

  TEST_CASE("writable_span_and_commit")
  {
    tc::SimpleBuffer<8> buf;

    // First span: should give us requested 5 bytes.
    auto w1 = buf.writable_span(5);
    CHECK(w1.size() == 5);
    std::memcpy(w1.data(), "abcde", 5);
    buf.commit(5);

    CHECK(buf.used_space() == 5);
    CHECK(buf.free_space() == 3);
    CHECK(peek_string(buf, 5) == "abcde");

    // Second span: only 3 bytes free now.
    auto w2 = buf.writable_span(10);
    CHECK(w2.size() == 3);
    std::memcpy(w2.data(), "XYZ", 3);
    buf.commit(3);

    CHECK(buf.full());
    CHECK(buf.used_space() == buf.capacity());
    CHECK(peek_string(buf, 8) == "abcdeXYZ");
  }

  TEST_CASE("readable_span_and_consume")
  {
    tc::SimpleBuffer<8> buf;

    write_string(buf, "abcdef");
    CHECK(buf.used_space() == 6);

    auto r1 = buf.readable_span(4);
    CHECK(r1.size() == 4);
    CHECK(read_string(r1) == "abcd");

    buf.consume(2);
    CHECK(buf.used_space() == 4);

    auto r2 = buf.readable_span(8);
    CHECK(r2.size() == 4);
    CHECK(read_string(r2) == "cdef");

    // consume(0) should be a no-op
    buf.consume(0);
    CHECK(buf.used_space() == 4);

    // Consuming more than used_space() should just clear the buffer.
    buf.consume(10);
    CHECK(buf.empty());
    CHECK(buf.used_space() == 0);
    CHECK(buf.free_space() == buf.capacity());
  }

  TEST_CASE("clear_resets_state")
  {
    tc::SimpleBuffer<8> buf;

    write_string(buf, "abc");
    CHECK_FALSE(buf.empty());
    CHECK(buf.used_space() == 3);

    buf.clear();
    CHECK(buf.empty());
    CHECK(buf.used_space() == 0);
    CHECK(buf.free_space() == buf.capacity());

    // After clear(), buffer should be reusable.
    write_string(buf, "xyz");
    CHECK(buf.used_space() == 3);
    CHECK(peek_string(buf, 3) == "xyz");
  }
}
