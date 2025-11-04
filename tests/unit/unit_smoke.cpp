#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

import common.logging;
import storage.engine;
import net.utils;

TEST_CASE("logging_stub")
{
  tskv::common::logging_stub();
  CHECK(true);
}

TEST_CASE("engine_stub")
{
  tskv::storage::engine_stub();
  CHECK(true);
}

TEST_CASE("net.utils::is_valid_port")
{
  CHECK(tskv::net::is_valid_port(80));
  CHECK_FALSE(tskv::net::is_valid_port(0));
  CHECK_FALSE(tskv::net::is_valid_port(70000));
}
