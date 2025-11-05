#include <doctest.h>

#include <cstdint>

import common.key_array;

namespace tc = tskv::common;

TEST_SUITE("common.key_array")
{
  using A = tc::key_array_t<std::uint64_t, "x", "y">;

  TEST_CASE("zero_initialize")
  {
    A a{}; // zero-initialized

    CHECK(a.data[0] == 0);
    CHECK(a.data[1] == 0);
    CHECK(a.get<"x">() == 0);
    CHECK(a.get<"y">() == 0);
  }

  TEST_CASE("get")
  {
    A a{};

    a.get<"x">() = 42;
    a.get<"y">() = 7;

    CHECK(a.get<"x">() == 42);
    CHECK(a.get<"y">() == 7);

    CHECK(a.data[0] == 42);
    CHECK(a.data[1] == 7);
  }

  TEST_CASE("operator+=")
  {
    using Big   = tc::key_array_t<std::uint64_t, "a", "b", "c">;
    using Small = tc::key_array_t<std::uint64_t, "a", "c">;

    Big   big{};
    Small small{};

    big.get<"a">() = 1;
    big.get<"b">() = 10;
    big.get<"c">() = 100;

    small.get<"a">() = 2;
    small.get<"c">() = 3;

    big += small;

    CHECK(big.get<"a">() == 3); // 1 + 2
    CHECK(big.get<"b">() == 10); // unchanged
    CHECK(big.get<"c">() == 103); // 100 + 3
  }
}
