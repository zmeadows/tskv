#if defined(__GNUC__) || defined(__clang__)
#  define TSKV_INLINE inline __attribute__((always_inline))
#else
#  define TSKV_INLINE inline
#endif

#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(gnu::cold)
#    define TSKV_COLD_PATH [[gnu::cold]] [[gnu::noinline]]
#  endif
#endif

#ifndef TSKV_COLD_PATH
#  if defined(__clang__) || defined(__GNUC__)
#    define TSKV_COLD_PATH __attribute__((cold, noinline))
#  else
#    define TSKV_COLD_PATH
#  endif
#endif
