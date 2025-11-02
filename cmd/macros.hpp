#pragma once

#define TRY_ARG_ASSIGN(_args, _outvar, _key)                                                       \
  do {                                                                                             \
    if ((_args).has_key(_key)) {                                                                   \
      using _OutT = decltype(_outvar);                                                             \
      auto res    = args.pop_kv<_OutT>(_key);                                                      \
      if (!res) {                                                                                  \
        return std::unexpected(res.error());                                                       \
      }                                                                                            \
      if constexpr (std::is_trivially_move_assignable_v<_OutT>) {                                  \
        (_outvar) = *res;                                                                          \
      }                                                                                            \
      else {                                                                                       \
        (_outvar) = std::move(*res);                                                               \
      }                                                                                            \
    }                                                                                              \
  } while (0)
