module;

#include <array>
#include <cstdint>
#include <string_view>

export module storage.wal;

import common.enum_traits;

namespace tc = tskv::common;

export namespace tskv::storage {

enum class WALSyncPolicy : uint8_t { Append, FDataSync };

} // namespace tskv::storage

export namespace tskv::common {

template <> struct enum_traits<tskv::storage::WALSyncPolicy> {
  static constexpr std::array<std::pair<tskv::storage::WALSyncPolicy, std::string_view>, 3> entries{
    {
      {tskv::storage::WALSyncPolicy::Append, "append"},
      {tskv::storage::WALSyncPolicy::FDataSync, "fdatasync"},
    }};
};

} // namespace tskv::common
