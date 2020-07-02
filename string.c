#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "string.h"

inline size_t cb_strlen(cb_str s)
{
	return s.len;
}

inline const char *cb_strptr(cb_str s)
{
	return s.chars;
}

cb_str cb_str_from_cstr(const char *str, size_t len)
{
	char *buf;

	buf = malloc(len + 1);
	memcpy(buf, str, len);
	buf[len] = 0;

	return (cb_str) { len, buf };
}

int cb_str_eq_cstr(cb_str s, const char *cstr, size_t len)
{
	if (len != cb_strlen(s))
		return 0;
	return !strncmp(cb_strptr(s), cstr, cb_strlen(s));
}

void cb_str_free(cb_str s)
{
	free(s.chars);
}
