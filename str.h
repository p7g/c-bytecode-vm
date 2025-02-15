#ifndef cb_string_h
#define cb_string_h

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "cb_util.h"

/* We can fit short strings in the struct in the same space it would take to
 * store a pointer to them. */
#define CB_STR_INLINE_SIZE (sizeof(size_t) / sizeof(char))
#define CB_STR_CAN_INLINE(S) (S < CB_STR_INLINE_SIZE)

typedef struct cb_str {
	size_t len, ncodepoints;
	union {
		/* Terminated with a NUL character */
		char *big;
		char small[CB_STR_INLINE_SIZE];
	} chars;
} cb_str;

size_t cb_strlen(cb_str s);
size_t cb_str_ncodepoints(cb_str s);
char *cb_strptr(cb_str *s);
CB_NODISCARD ssize_t cb_str_from_cstr(const char *str, size_t len, cb_str *out);
int cb_str_eq_cstr(cb_str s, const char *cstr, size_t len);
char *cb_strdup_cstr(cb_str str);
cb_str cb_strdup(cb_str str);
CB_NODISCARD ssize_t cb_str_take_cstr(char *str, size_t len, cb_str *s);
void cb_str_init(struct cb_str *str, size_t len);
int cb_strcmp(cb_str a, cb_str b);
CB_NODISCARD ssize_t cb_str_check_utf8(cb_str *str);
const char *cb_str_errmsg(ssize_t err);
CB_NODISCARD ssize_t cb_str_read_char(cb_str s, size_t pos, int32_t *c);

void cb_str_free(cb_str s);

#endif