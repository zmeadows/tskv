#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <print>

#include "macros.hpp"
#include "tskv/common/logging.hpp"

namespace fs = std::filesystem;

import tskv.cmd.args;
import tskv.cmd.version;
import tskv.common.enum_traits;
import tskv.common.files;
import tskv.common.logging;
import tskv.net.server;
import tskv.net.utils;
import tskv.storage.wal;

namespace tc  = tskv::common;
namespace ts  = tskv::storage;
namespace tn  = tskv::net;
namespace cmd = tskv::cmd;

static void print_help()
{
  using std::println;

  println("tskv server — usage:");
  println("  server [--host <ip|name>] [--port <1-65535>] [--data-dir <path>]");
  println("         [--wal-sync <append|fdatasync>] [--memtable-bytes <n>]");
  println("         [--max-connections <n>] [--version] [--help] [--dry-run]");
  println("");

  println("Options:");
  println("  --host <ip|name>           Bind address (default: 0.0.0.0)");
  println("  --port <n>                 TCP port (default: 7070)");
  println("  --data-dir <path>          Data directory (default: ./data)");
  println("  --wal-sync <mode>          WAL durability: append | fdatasync (default: append)");
  println("  --memtable-bytes <n>       Target memtable size in bytes (default: 67108864)");
  println("  --max-connections <n>      Max concurrent connections (default: 1024)");
  println("  --dry-run                  Print CLI args and exit");
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

  static ServerConfig from_cli(cmd::CmdLineArgs& args)
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
    TSKV_ASSERT(
      tn::is_valid_port(config.port), "invalid_port: expected 1..65535 (got {})", config.port);

    {
      auto clean_data_dir = tc::standardize_path(config.data_dir);
      TSKV_ASSERT(clean_data_dir, "invalid_data_dir: {}", config.data_dir.string());
      config.data_dir = *clean_data_dir;

      const bool data_dir_exists = fs::exists(config.data_dir);
      const bool parent_bad = !data_dir_exists && !tc::can_create_in(config.data_dir.parent_path());
      const bool leaf_bad   = data_dir_exists && !tc::can_create_in(config.data_dir);
      TSKV_ASSERT(!parent_bad && !leaf_bad, "invalid_data_dir: {}", config.data_dir.string());
    }

    return config;
  }

  void print() const
  {
    std::print("tskv server CFG ::");
    std::print(" host={}", this->host);
    std::print(" port={}", this->port);
    std::print(" data-dir={}", this->data_dir.string());
    std::print(" wal-sync={}", tc::to_string(this->wal_sync_policy));
    std::print(" memtable-bytes={}", this->memtable_bytes);
    std::print(" max-connections={}", this->max_connections);
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

  const auto config = ServerConfig::from_cli(args);

  const bool dry_run = args.pop_flag("dry-run");

  args.detect_unused_args();

  if (dry_run) {
    config.print();
    return EXIT_SUCCESS;
  }

  tn::scratch_main();

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
