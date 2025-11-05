module;

#include <algorithm>
#include <string_view>

export module common.string_literal;

export namespace tskv::common {

template <std::size_t N>
struct string_literal {
  char data[N]{};

  constexpr string_literal(char const (&str)[N]) { std::copy_n(str, N, data); }

  static constexpr std::size_t size = N - 1;

  [[nodiscard]] constexpr operator std::string_view() const noexcept
  {
    return std::string_view{data, N - 1}; // drop trailing '\0'
  }

  constexpr auto operator<=>(string_literal const&) const = default;
};

} // namespace tskv::common
