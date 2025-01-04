#ifndef cb_string_h
#define cb_string_h

#include <stddef.h>
#include <stdint.h>

/* We can fit short strings in the struct in the same space it would take to
 * store a pointer to them. */
#define CB_STR_INLINE_SIZE (sizeof(size_t) / sizeof(char))
#define CB_STR_CAN_INLINE(S) (S < CB_STR_INLINE_SIZE)

typedef struct cb_str {
	/* Maximally the allocated size of chars minus 1 */
	size_t len;
	union {
		/* Terminated with a NUL character */
		char *big;
		char small[CB_STR_INLINE_SIZE];
	} chars;
} cb_str;

size_t cb_strlen(cb_str s);
char *cb_strptr(cb_str *s);
cb_str cb_str_from_cstr(const char *str, size_t len);
int cb_str_eq_cstr(cb_str s, const char *cstr, size_t len);
uint32_t cb_str_at(cb_str s, size_t idx);
char *cb_strdup_cstr(cb_str str);
cb_str cb_strdup(cb_str str);
cb_str cb_str_take_cstr(char *str, size_t len);
void cb_str_init(struct cb_str *str, size_t len);

void cb_str_free(cb_str s);

#endif