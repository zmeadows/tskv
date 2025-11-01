#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <print>

#include "macros.hpp"

import common.enum_traits;
import storage.wal;
import cmd.args;

namespace tc  = tskv::common;
namespace ts  = tskv::storage;
namespace cmd = tskv::cmd;

static void print_help()
{
  using std::println;

  println("tskv client — usage:");
  println("  client [--host <ip|name>] [--port <1-65535>] [--timeout-ms <n>]");
  println("         [--version] [--help]");
  println("");

  println("Options:");
  println("  --host <ip|name>           Bind address (default: 127.0.0.1)");
  println("  --port <n>                 TCP port (default: 7070)");
  println("  --timeout-ms <n>           Timeout [milliseconds] (default: 2000)");
  println("  --version                  Print version and exit");
  println("  --help                     Show this help and exit");
}

struct ClientConfig {
  std::string host       = "127.0.0.1";
  uint16_t    port       = 7070;
  uint32_t    timeout_ms = 2000;

  static std::expected<ClientConfig, std::string> from_cli(cmd::CmdLineArgs& args)
  {
    ClientConfig config;

    // 1) Parse
    TRY_ARG_ASSIGN(args, config.host, "host");
    TRY_ARG_ASSIGN(args, config.port, "port");
    TRY_ARG_ASSIGN(args, config.timeout_ms, "timeout-ms");

    // 2) Validate
    if (config.port == 0 || config.port > 65535) {
      return std::unexpected(std::format("Invalid port: {}", config.port));
    }

    return config;
  }

  void print() const
  {
    std::println(std::cerr, "CLIENT CONFIGURATION:");
    std::println(std::cerr, "\thost: {}", this->host);
    std::println(std::cerr, "\tport: {}", this->port);
    std::println(std::cerr, "\ttimeout-ms: {}", this->timeout_ms);
  }
};

static void print_version()
{
}

int main_(int argc, char** argv)
{
  cmd::CmdLineArgs args(argc, argv);

  if (auto res = args.parse(); !res) {
    std::cerr << "CLI parse error: \n\t" << res.error() << '\n';
    return EXIT_FAILURE;
  }

  if (args.pop_flag("help")) {
    print_help();
    return EXIT_SUCCESS;
  }

  if (args.pop_flag("version")) {
    print_version();
    return EXIT_SUCCESS;
  }

  const auto config = ClientConfig::from_cli(args);
  if (!config) {
    std::println(std::cerr, "{}", config.error());
    return EXIT_FAILURE;
  }

  config->print();

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
