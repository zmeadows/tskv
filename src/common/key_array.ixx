module;

#include <array>
#include <concepts>
#include <functional>
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

  template <typename F>
  constexpr void for_each_key(F&& f) &
  {
    (f.template operator()<Keys>(get<Keys>()), ...);
  }

  template <typename F>
  constexpr void for_each_key(F&& f) const&
  {
    (f.template operator()<Keys>(get<Keys>()), ...);
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

// clang-format off
template <typename TG, tc::string_literal... GKeys,
          typename TL, tc::string_literal... LKeys,
          typename F>
constexpr void bitransform_key_arrays(
    key_array<TG, key_set<GKeys...>>& global,
    const key_array<TL, key_set<LKeys...>>& local,
    F&& f)
{
  ( // fold expression
    [&] {
      if constexpr (key_set<LKeys...>::template contains<GKeys>()) {
        auto& g = global.template get<GKeys>();
        const auto& l = local.template get<GKeys>();

        // If F is directly invocable with (g, l), use that:
        if constexpr (std::invocable<F&, decltype(g), decltype(l)>) {
          std::invoke(f, g, l);
        } else {
          // Otherwise assume it's a key-aware callable: f.template operator()<K>(g, l)
          f.template operator()<GKeys>(g, l);
        }
      }
    }(),
    ...
  );
}
// clang-format on

} // namespace tskv::common
