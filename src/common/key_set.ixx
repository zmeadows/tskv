module;

#include <array>

export module common.key_set;

export import common.string_literal;

namespace tc = tskv::common;

export namespace tskv::common {

template <tc::string_literal... Keys>
struct key_set {
  static constexpr std::size_t size = sizeof...(Keys);

  // Simple, always-constant-expression membership test
  template <tc::string_literal K>
  [[nodiscard]] static consteval bool contains()
  {
    return ((Keys == K) || ...);
  }

  // Index lookup for keys we *know* are present
  template <tc::string_literal K>
  [[nodiscard]] static consteval std::size_t index_of()
  {
    constexpr std::array<bool, size> matches{(Keys == K)...};

    for (std::size_t i = 0; i < size; ++i) {
      if (matches[i]) {
        return i;
      }
    }

    // Only instantiated if you actually call index_of with a missing key
    []<bool present = contains<K>()>() {
      static_assert(present, "key_set::index_of<K>: key is not in this key_set");
    }();

    return 0; // unreachable, but keeps the compiler happy
  }
};

template <typename Set, tc::string_literal... ToAdd>
struct key_set_append_unique;

// Base case: nothing left to add
template <tc::string_literal... Base>
struct key_set_append_unique<key_set<Base...>> {
  using type = key_set<Base...>;
};

// Recursive case: process Head, then Tail...
template <tc::string_literal... Base, tc::string_literal Head, tc::string_literal... Tail>
struct key_set_append_unique<key_set<Base...>, Head, Tail...> {
private:
  using current_set = key_set<Base...>;

  // If Head is already in Base..., keep current_set; otherwise append Head.
  using with_head =
    std::conditional_t<current_set::template contains<Head>(), current_set, key_set<Base..., Head>>;

public:
  using type = typename key_set_append_unique<with_head, Tail...>::type;
};

// Convenience alias
template <typename Set, tc::string_literal... ToAdd>
using key_set_append_unique_t = typename key_set_append_unique<Set, ToAdd...>::type;

// -------------------------------------------------------------
// key_set_union
// -------------------------------------------------------------

template <typename Set1, typename Set2>
struct key_set_union;

// Specialization for key_set<...> and key_set<...>
template <tc::string_literal... K1, tc::string_literal... K2>
struct key_set_union<key_set<K1...>, key_set<K2...>> {
  using type = key_set_append_unique_t<key_set<K1...>, K2...>;
};

// Convenience alias
template <typename Set1, typename Set2>
using key_set_union_t = typename key_set_union<Set1, Set2>::type;

} // namespace tskv::common
