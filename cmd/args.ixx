module;

#include <charconv>
#include <expected>
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

export module cmd.args;

import storage.wal;
import common.enum_traits;

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

  [[nodiscard]] std::expected<void, std::string> parse()
  {
    auto bail = [&]() -> void {
      _kvs.clear();
      _flags.clear();
    };

    size_t i = 1;
    while (i < _args.size()) {
      std::string_view tok(_args[i]);
      if (!tok.starts_with("--")) {
        bail();
        return std::unexpected(
          std::format("bad_cli_arg: expected flag starting with -- (got \"{}\")", tok));
      }

      tok.remove_prefix(2); // strip "--"

      if (tok.empty()) {
        bail();
        return std::unexpected(
          std::format("bad_cli_arg: found unsupported stand-alone double dash \"--\""));
      }

      if (_kvs.contains(tok) || _flags.contains(tok)) {
        bail();
        return std::unexpected(
          std::format("duplicate_ci_args: key/flag specified twice --{}", tok));
      }

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

    return {};
  }

  [[nodiscard]] bool has_key(std::string_view key) const noexcept { return _kvs.contains(key); }

  template <typename V> std::expected<V, std::string> pop_kv(std::string_view key)
  {
    auto kit = _kvs.find(key);
    if (kit == _kvs.end()) {
      return std::unexpected(std::format("missing_key: key not found ({})", key));
    }

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
        return std::unexpected(errmsg());
      }
    }
    else if constexpr (std::is_same_v<V, ts::WALSyncPolicy>) {
      if (auto o_policy = tc::from_string<ts::WALSyncPolicy>(sv); o_policy.has_value()) {
        return *o_policy;
      }
      return std::unexpected(errmsg());
    }
    else if constexpr (std::is_integral_v<V> && !std::is_same_v<V, bool>) {
      const char* first = sv.data();
      const char* last  = first + sv.size();

      V out{};
      auto [ptr, ec] = std::from_chars(first, last, out, 10); // base-10; no whitespace
      if (ec == std::errc{}) {
        if (ptr != last) {
          return std::unexpected(errmsg());
        }
        return out;
      }
      return std::unexpected(errmsg());
    }
    else {
      static_assert(!sizeof(V), "pop_value: unsupported type");
    }
  }

  [[nodiscard]] bool pop_flag(std::string_view flag) noexcept { return _flags.erase(flag) != 0; }

  std::expected<void, std::string> detect_unused_args()
  {
    if (_kvs.empty() && _flags.empty()) {
      return {};
    }

    std::string errmsg = "unused_cli_args: ";

    for (const auto& [k, v] : _kvs) {
      errmsg.append(std::format("--{}={} ", k, v));
    }

    for (const auto& f : _flags) {
      errmsg.append(std::format("--{} ", f));
    }

    return std::unexpected(errmsg);
  }
};

} // namespace tskv::cmd
