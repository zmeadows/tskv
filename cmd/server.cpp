#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <iostream>
#include <print>

#include "macros.hpp"

// TODO[@zmeadows][P0]: add --version support via CMake?

namespace fs = std::filesystem;

import common.enum_traits;
import common.files;
import storage.wal;
import cmd.args;
import cmd.version;

namespace tc  = tskv::common;
namespace ts  = tskv::storage;
namespace cmd = tskv::cmd;

static void print_help()
{
  using std::println;

  println("tskv server — usage:");
  println("  server [--host <ip|name>] [--port <1-65535>] [--data-dir <path>]");
  println("         [--wal-sync <append|fdatasync>] [--memtable-bytes <n>]");
  println("         [--max-connections <n>] [--version] [--help]");
  println("");

  println("Options:");
  println("  --host <ip|name>           Bind address (default: 0.0.0.0)");
  println("  --port <n>                 TCP port (default: 7070)");
  println("  --data-dir <path>          Data directory (default: ./data)");
  println("  --wal-sync <mode>          WAL durability: append | fdatasync (default: append)");
  println("  --memtable-bytes <n>       Target memtable size in bytes (default: 67108864)");
  println("  --max-connections <n>      Max concurrent connections (default: 1024)");
  println("  --version                  Print version and exit");
  println("  --help                     Show this help and exit");
}

struct ServerConfig {
  std::string       host            = "0.0.0.0";
  uint16_t          port            = 7070;
  fs::path          data_dir        = "./data";
  ts::WALSyncPolicy wal_sync_policy = ts::WALSyncPolicy::Append;
  uint64_t          memtable_bytes  = 67108864;
  uint32_t          max_connections = 1024;

  static std::expected<ServerConfig, std::string> from_cli(cmd::CmdLineArgs& args)
  {
    ServerConfig config;

    // 1) Parse
    TRY_ARG_ASSIGN(args, config.host, "host");
    TRY_ARG_ASSIGN(args, config.port, "port");
    TRY_ARG_ASSIGN(args, config.data_dir, "data-dir");
    TRY_ARG_ASSIGN(args, config.wal_sync_policy, "wal-sync");
    TRY_ARG_ASSIGN(args, config.memtable_bytes, "memtable-bytes");
    TRY_ARG_ASSIGN(args, config.max_connections, "max-connections");

    // 2) Validate
    if (config.port == 0) {
      return std::unexpected(std::format("Invalid port: 0"));
    }

    auto clean_data_dir = tc::standardize_path(config.data_dir);
    if (!clean_data_dir) {
      return std::unexpected(clean_data_dir.error());
    }
    config.data_dir = *clean_data_dir;

    if (!tc::is_writeable(config.data_dir.parent_path())) {
      return std::unexpected(
        std::format("Failed to detect data-dir write-access at: {}", config.data_dir.string()));
    }

    return config;
  }

  void print() const
  {
    std::println(std::cerr, "SERVER CONFIGURATION:");
    std::println(std::cerr, "\thost: {}", this->host);
    std::println(std::cerr, "\tport: {}", this->port);
    std::println(std::cerr, "\tdata-dir: {}", this->data_dir.string());
    std::println(std::cerr, "\twal-sync: {}", tc::to_string(this->wal_sync_policy));
    std::println(std::cerr, "\tmemtable-bytes: {}", this->memtable_bytes);
    std::println(std::cerr, "\tmax-connections: {}", this->max_connections);
  }
};

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
    cmd::print_version();
    return EXIT_SUCCESS;
  }

  const auto config = ServerConfig::from_cli(args);
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
