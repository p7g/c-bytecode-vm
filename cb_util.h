#ifndef CB_UTIL_H
#define CB_UTIL_H

#define CB_INLINE inline __attribute__((always_inline))
#define CB_NODISCARD __attribute__((warn_unused_result))
#define CB_LIKELY(X) (X) /*__builtin_expect(!!(X), 1)*/
#define CB_UNLIKELY(X) (X) /*__builtin_expect(!!(X), 0)*/

#endif