module;

#include <optional>
#include <string_view>

export module tskv.common.enum_traits;

export namespace tskv::common {

template <class E> struct enum_traits;

template <class E> constexpr std::string_view to_string(E e) noexcept
{
  for (auto [v, name] : enum_traits<E>::entries)
    if (v == e)
      return name;
  return "<unknown>";
}

template <class E> constexpr std::optional<E> from_string(std::string_view s) noexcept
{
  for (auto [v, name] : enum_traits<E>::entries)
    if (s == name)
      return v;
  return std::nullopt;
}

} // namespace tskv::common
