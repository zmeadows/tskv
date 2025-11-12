module;

#include <array>
#include <chrono>
#include <expected>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

export module tskv.common.files;

namespace fs = std::filesystem;

std::string random_probe_suffix()
{
  std::array<unsigned char, 16> bytes{};
  {
    thread_local std::mt19937_64 gen([] {
      uint64_t seed =
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
        reinterpret_cast<uintptr_t>(&random_probe_suffix);
      try {
        std::random_device rd;
        seed ^= (static_cast<uint64_t>(rd()) << 32) ^ rd();
      }
      catch (...) { // NOLINT(bugprone-empty-catch)
      }
      return std::mt19937_64{seed};
    }());

    for (size_t i = 0; i < bytes.size(); i += 8) {
      uint64_t r = gen();
      for (int j = 0; j < 8; ++j)
        bytes[i + j] = static_cast<unsigned char>(r >> (j * 8));
    }
  }

  static constexpr char hexdig[] = "0123456789abcdef";

  std::string out;
  out.resize(32);
  for (size_t i = 0; i < bytes.size(); ++i) {
    out[2 * i]     = hexdig[(bytes[i] >> 4) & 0xF];
    out[2 * i + 1] = hexdig[bytes[i] & 0xF];
  }
  return out;
}

export namespace tskv::common {

std::expected<fs::path, std::string> standardize_path(const fs::path& p)
{
  if (p.empty()) {
    return std::unexpected("empty path specified.");
  }

  fs::path p_clean = fs::absolute(p).lexically_normal();

  if (fs::exists(p_clean)) {
    return fs::canonical(p_clean);
  }

  fs::path parent = p_clean.parent_path();
  if (!fs::exists(parent)) {
    return std::unexpected(std::format("parent directory doesn't exist ({})", parent.string()));
  }
  parent = fs::canonical(parent);

  return (parent / p.filename()).lexically_normal();
}

bool can_create_in(const fs::path& dir) noexcept
{
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return false;

  // Try a few times in the astronomically unlikely event of a collision.
  for (int attempt = 0; attempt < 6; ++attempt) {
    fs::path probe = dir / (".__mkdir_probe-" + random_probe_suffix());

    // Try to create a subdirectory (atomic wrt existing names).
    if (!fs::create_directory(probe, ec)) {
      if (ec)
        return false; // permission or other error
      continue; // exists==true (collision) -> retry
    }

    std::error_code ignore;
    fs::remove(probe, ignore);

    return true;
  }
  return false;
}

} // namespace tskv::common
