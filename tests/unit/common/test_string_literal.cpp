#include <doctest.h>

#include <string_view>

import common.string_literal;
namespace tc = tskv::common;

TEST_SUITE("common.string_literal")
{
  TEST_CASE("string_literal.basic_properties")
  {
    constexpr tc::string_literal hello{"hello"};

    static_assert(hello.size == 5);
    CHECK(hello.size == 5);

    std::string_view sv = hello;
    CHECK(sv == "hello");
    CHECK(sv[0] == 'h');
    CHECK(sv[4] == 'o');

    CHECK(sv > "apple");
  }
}
