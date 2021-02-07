#ifndef cb_string_h
#define cb_string_h

#include <stddef.h>
#include <stdint.h>

typedef struct cb_str {
	/* Maximally the allocated size of chars minus 1 */
	size_t len;
	/* Terminated with a NUL character */
	char *chars;
} cb_str;

size_t cb_strlen(cb_str s);
const char *cb_strptr(cb_str s);
cb_str cb_str_from_cstr(const char *str, size_t len);
int cb_str_eq_cstr(cb_str s, const char *cstr, size_t len);
uint32_t cb_str_at(cb_str s, size_t idx);
char *cb_cstrdup(const char *str, size_t len);
char *cb_strdup_cstr(cb_str str);
cb_str cb_strdup(cb_str str);

void cb_str_free(cb_str s);

#endif
