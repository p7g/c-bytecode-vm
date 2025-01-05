#ifndef CB_UTIL_H
#define CB_UTIL_H

#define CB_INLINE inline __attribute__((always_inline))

#define CB_PRAGMA(X) _Pragma(#X)

#define CB_DIAGNOSTIC_PUSH CB_PRAGMA(GCC diagnostic push)
#define CB_DIAGNOSTIC_POP CB_PRAGMA(GCC diagnostic pop)

#define CB_IGNORE_WARNING(W) \
	CB_DIAGNOSTIC_PUSH \
	CB_DIAGNOSTIC_IGNORE(W)
#define CB_IGNORE_WARNING_END CB_DIAGNOSTIC_POP

#if defined(__has_warning)
# if __has_warning("-Wuse-after-free")
#  define CB_IGNORE_USE_AFTER_FREE CB_IGNORE_WARNING("-Wuse-after-free")
#  define CB_IGNORE_USE_AFTER_FREE_END CB_IGNORE_WARNING_END
# else
#  define CB_IGNORE_USE_AFTER_FREE
#  define CB_IGNORE_USE_AFTER_FREE_END
# endif
#else
# define CB_IGNORE_USE_AFTER_FREE
# define CB_IGNORE_USE_AFTER_FREE_END
#endif

#endif