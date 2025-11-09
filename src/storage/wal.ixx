module;

#include <array>
#include <cstdint>
#include <string_view>

export module tskv.storage.wal;

import tskv.common.enum_traits;
namespace tc = tskv::common;

export namespace tskv::storage {

enum class WALSyncPolicy : uint8_t { Append, FDataSync };

} // namespace tskv::storage

namespace ts = tskv::storage;

export namespace tskv::common {

template <>
struct enum_traits<ts::WALSyncPolicy> {
  static constexpr std::array<std::pair<ts::WALSyncPolicy, std::string_view>, 3> entries{{
    {ts::WALSyncPolicy::Append, "append"},
    {ts::WALSyncPolicy::FDataSync, "fdatasync"},
  }};
};

} // namespace tskv::common
