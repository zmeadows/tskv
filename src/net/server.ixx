module;

#include <cassert>
#include <filesystem>
#include <netdb.h>
#include <print>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "tskv/common/logging.hpp"

export module tskv.net.server;

import tskv.common.buffer;
import tskv.common.enum_traits;
import tskv.common.logging;
import tskv.net.channel;
import tskv.net.socket;
import tskv.storage.wal;

namespace tc = tskv::common;
namespace ts = tskv::storage;
namespace fs = std::filesystem;

export namespace tskv::net {

struct ServerConfig {
  std::string       host            = "localhost";
  uint16_t          port            = 7070;
  fs::path          data_dir        = "./data";
  ts::WALSyncPolicy wal_sync_policy = ts::WALSyncPolicy::Append;
  uint64_t          memtable_bytes  = 67108864;
  uint32_t          max_connections = 1024;

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

} // namespace tskv::net
