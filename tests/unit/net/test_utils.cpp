#include <doctest.h>

import net.utils;

TEST_SUITE("net.utils")
{
  TEST_CASE("is_valid_port")
  {
    CHECK(tskv::net::is_valid_port(80));
    CHECK_FALSE(tskv::net::is_valid_port(0));
    CHECK_FALSE(tskv::net::is_valid_port(70000));
  }
}
