module;

#include <expected>
#include <filesystem>
#include <fstream>

export module common.files;

namespace fs = std::filesystem;

export namespace tskv::common {

std::expected<fs::path, std::string> standardize_path(const fs::path& p)
{
  if (p.empty()) {
    return std::unexpected("Empty path specified.");
  }

  fs::path p_clean = fs::absolute(p).lexically_normal();

  if (fs::exists(p_clean)) {
    return fs::canonical(p_clean);
  }

  fs::path parent = p_clean.parent_path();
  if (!fs::exists(parent)) {
    return std::unexpected(std::format("Parent directory doesn't exist: {}", parent.string()));
  }
  parent = fs::canonical(parent);

  return (parent / p.filename()).lexically_normal();
}

bool is_writeable(const fs::path& pdir)

{
  try {
    if (!fs::is_directory(pdir)) {
      return false;
    }
    const std::filesystem::path tmp_path = pdir / "tskv_test.tmp";

    std::ofstream tmp_file(tmp_path);
    if (!tmp_file.is_open()) {
      return false;
    }
    tmp_file.close();

    std::filesystem::remove(tmp_path);
    return true;
  }
  catch (const std::exception& e) {
    return false;
  }
}

} // namespace tskv::common
