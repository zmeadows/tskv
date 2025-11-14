module;

#include <charconv>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "tskv/common/logging.hpp"

export module tskv.cmd.args;

import tskv.storage.wal;
import tskv.common.enum_traits;
import tskv.common.logging;

namespace ts = tskv::storage;
namespace tc = tskv::common;

namespace fs = std::filesystem;

export namespace tskv::cmd {

class CmdLineArgs {
  const std::vector<std::string> _args;

  std::map<std::string_view, std::string_view> _kvs;
  std::set<std::string_view>                   _flags;

public:
  CmdLineArgs(int argc, char** argv) : _args(argv, argv + argc) {}

  CmdLineArgs(const CmdLineArgs&)            = delete;
  CmdLineArgs& operator=(const CmdLineArgs&) = delete;
  CmdLineArgs(CmdLineArgs&&)                 = delete;
  CmdLineArgs& operator=(CmdLineArgs&&)      = delete;

  void parse()
  {
    size_t i = 1;
    while (i < _args.size()) {
      std::string_view tok(_args[i]);
      TSKV_REQUIRE(
        tok.starts_with("--"), "bad_cli_arg: expected flag starting with -- (got \"{}\")", tok);

      tok.remove_prefix(2); // strip "--"

      TSKV_REQUIRE(!tok.empty(), "bad_cli_arg: found unsupported stand-alone double dash \"--\"");

      TSKV_REQUIRE(!_kvs.contains(tok) && !_flags.contains(tok),
        "duplicate_ci_args: key/flag specified twice --{}",
        tok);

      // Handle --key value  OR a standalone --flag
      if (i + 1 < _args.size() && !_args[i + 1].starts_with("--")) {
        _kvs.emplace(tok, std::string_view(_args[i + 1]));
        i += 2;
      }
      else {
        _flags.insert(tok);
        i += 1;
      }
    }

    return;
  }

  [[nodiscard]] bool has_key(std::string_view key) const noexcept { return _kvs.contains(key); }

  template <typename V>
  V pop_kv(std::string_view key)
  {
    auto kit = _kvs.find(key);
    TSKV_REQUIRE(kit != _kvs.end(), "missing_key: key not found ({})", key);

    std::string_view sv = kit->second;
    _kvs.erase(kit);

    auto errmsg = [&]() {
      return std::format("value_parse_err: failed to parse --{} value \"{}\"", key, sv);
    };

    if constexpr (std::is_same_v<V, std::string>) {
      return std::string(sv);
    }
    else if constexpr (std::is_same_v<V, fs::path>) {
      try {
        return fs::path(sv);
      }
      catch (...) {
        TSKV_REQUIRE(false, errmsg());
      }
    }
    else if constexpr (std::is_same_v<V, ts::WALSyncPolicy>) {
      auto o_policy = tc::from_string<ts::WALSyncPolicy>(sv);
      TSKV_REQUIRE(o_policy.has_value(), "invalid_wal_sync: unrecognized policy \"{}\"", sv);
      return *o_policy;
    }
    else if constexpr (std::is_integral_v<V> && !std::is_same_v<V, bool>) {
      const char* first = sv.data();
      const char* last  = first + sv.size();

      V out{};
      auto [ptr, ec] = std::from_chars(first, last, out, 10); // base-10; no whitespace
      TSKV_REQUIRE(ec == std::errc{}, errmsg());
      TSKV_REQUIRE(ptr == last, errmsg());
      return out;
    }
    else {
      static_assert(!sizeof(V), "pop_value: unsupported type");
    }
  }

  [[nodiscard]] bool pop_flag(std::string_view flag) noexcept { return _flags.erase(flag) != 0; }

  void enforce_no_unused_args()
  {
    if (_kvs.empty() && _flags.empty()) {
      return;
    }

    std::string errmsg = "unused_cli_args: ";

    for (const auto& [k, v] : _kvs) {
      errmsg.append(std::format("--{}={} ", k, v));
    }

    for (const auto& f : _flags) {
      errmsg.append(std::format("--{} ", f));
    }

    TSKV_REQUIRE(false, errmsg);
  }
};

} // namespace tskv::cmd
