module;

#include <array>
#include <string_view>

export module common.key_array;

export import common.key_set;

namespace tc = tskv::common;

export namespace tskv::common {

template <typename T, typename KeySet>
struct key_array;

template <typename T, tc::string_literal... Keys>
struct key_array<T, key_set<Keys...>> {
  using keys                        = key_set<Keys...>;
  static constexpr std::size_t size = keys::size;

  std::array<T, size> data{};

  static constexpr std::array<std::string_view, size> key_names{Keys...};

  template <tc::string_literal K>
  [[nodiscard]] constexpr T& get() & noexcept
  {
    constexpr std::size_t idx = keys::template index_of<K>();
    return data[idx];
  }

  template <tc::string_literal K>
  [[nodiscard]] constexpr const T& get() const& noexcept
  {
    constexpr std::size_t idx = keys::template index_of<K>();
    return data[idx];
  }

  template <tc::string_literal... OtherKeys>
    requires requires(T& a, const T& b) { a += b; } && (keys::template contains<OtherKeys>() && ...)
  constexpr key_array& operator+=(key_array<T, key_set<OtherKeys...>> const& other)
  {
    ((get<OtherKeys>() += other.template get<OtherKeys>()), ...);
    return *this;
  }
};

template <typename T, tc::string_literal... Keys>
using key_array_t = key_array<T, key_set<Keys...>>;

} // namespace tskv::common
