#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <print>

#include "macros.hpp"
#include "tskv/common/logging.hpp"

import tskv.common.enum_traits;
import tskv.common.logging;
import tskv.storage.wal;
import tskv.cmd.args;
import tskv.cmd.version;
import tskv.net.utils;

namespace tc  = tskv::common;
namespace ts  = tskv::storage;
namespace tn  = tskv::net;
namespace cmd = tskv::cmd;

static void print_help()
{
  using std::println;

  println("tskv client — usage:");
  println("  client [--host <ip|name>] [--port <1-65535>] [--timeout-ms <n>]");
  println("         [--version] [--help] [--dry-run]");
  println("");

  println("Options:");
  println("  --host <ip|name>           Bind address (default: 127.0.0.1)");
  println("  --port <n>                 TCP port (default: 7070)");
  println("  --timeout-ms <n>           Timeout [milliseconds] (default: 2000)");
  println("  --dry-run                  Print CLI args and exit");
  println("  --version                  Print version and exit");
  println("  --help                     Show this help and exit");
}

struct ClientConfig {
  std::string host       = "127.0.0.1";
  uint16_t    port       = 7070;
  uint32_t    timeout_ms = 2000;

  static ClientConfig from_cli(cmd::CmdLineArgs& args)
  {
    ClientConfig config;

    // 1) Parse
    TRY_ARG_ASSIGN(args, config.host, "host");
    TRY_ARG_ASSIGN(args, config.port, "port");
    TRY_ARG_ASSIGN(args, config.timeout_ms, "timeout-ms");

    // 2) Validate
    TSKV_ASSERT(
      tn::is_valid_port(config.port), "invalid_port: expected 1..65535 (got {})", config.port);

    return config;
  }

  void print() const
  {
    std::print("tskv client CFG ::");
    std::print(" host={}", this->host);
    std::print(" port={}", this->port);
    std::print(" timeout-ms={}", this->timeout_ms);
    std::print("\n");
  }
};

int main_(int argc, char** argv)
{
  cmd::CmdLineArgs args(argc, argv);

  args.parse();

  if (args.pop_flag("help")) {
    print_help();
    return EXIT_SUCCESS;
  }

  if (args.pop_flag("version")) {
    cmd::print_version();
    return EXIT_SUCCESS;
  }

  const auto config = ClientConfig::from_cli(args);

  const bool dry_run = args.pop_flag("dry-run");

  args.detect_unused_args();

  if (dry_run) {
    config.print();
    return EXIT_SUCCESS;
  }

  return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
  try {
    return main_(argc, argv);
  }
  catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return EXIT_FAILURE;
  }
}
