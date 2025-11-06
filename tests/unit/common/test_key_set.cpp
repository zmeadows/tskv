#include <doctest.h>

import common.key_set;

namespace tc = tskv::common;

using KS = tc::key_set<"a", "b", "c">;

namespace { // compile-time checks
static_assert(KS::contains<"a">());
static_assert(KS::contains<"b">());
static_assert(KS::contains<"c">());
static_assert(!KS::contains<"z">());

static_assert(KS::index_of<"a">() == 0);
static_assert(KS::index_of<"b">() == 1);
static_assert(KS::index_of<"c">() == 2);
} // namespace

TEST_SUITE("common.key_set")
{

  TEST_CASE("size")
  {
    CHECK(KS::size == 3);
  }

  TEST_CASE("contains")
  {
    CHECK(KS::contains<"a">());
    CHECK(KS::contains<"b">());
    CHECK(KS::contains<"c">());
    CHECK(!KS::contains<"z">());
  }

  TEST_CASE("index_of")
  {
    CHECK(KS::index_of<"a">() == 0);
    CHECK(KS::index_of<"b">() == 1);
    CHECK(KS::index_of<"c">() == 2);
  }

  TEST_CASE("union")
  {
    using KS2 = tc::key_set<"b", "c", "d">;
    using KSU = tc::key_set_union_t<KS, KS2>;

    CHECK(KSU::contains<"a">());
    CHECK(KSU::contains<"b">());
    CHECK(KSU::contains<"c">());
    CHECK(KSU::contains<"d">());
  }
}
