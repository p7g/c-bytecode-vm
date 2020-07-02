#ifndef cb_string_h
#define cb_string_h

#include <stddef.h>

typedef struct cb_str {
	size_t len;
	char *chars;
} cb_str;

size_t cb_strlen(cb_str s);
const char *cb_strptr(cb_str s);
cb_str cb_str_from_cstr(const char *str, size_t len);
int cb_str_eq_cstr(cb_str s, const char *cstr, size_t len);

void cb_str_free(cb_str s);

#endif
