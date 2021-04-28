#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "str.h"

inline size_t cb_strlen(cb_str s)
{
	return s.len;
}

inline char *cb_strptr(cb_str *s)
{
	char *ptr;

	if (CB_STR_CAN_INLINE(s->len))
		ptr = &s->chars.small[0];
	else
		ptr = s->chars.big;

	/* ensure it's NUL-terminated */
	ptr[s->len] = 0;
	return ptr;
}

void cb_str_init(struct cb_str *str, size_t len)
{
	str->len = len;
	if (!CB_STR_CAN_INLINE(len))
		str->chars.big = malloc(len + 1);
}

static char *str_init(struct cb_str *str, size_t len)
{
	cb_str_init(str, len);
	return cb_strptr(str);
}

cb_str cb_str_from_cstr(const char *str, size_t len)
{
	char *buf;
	cb_str s;

	buf = str_init(&s, len);
	memcpy(buf, str, len);
	buf[len] = 0;

	return s;
}

cb_str cb_str_take_cstr(char *str, size_t len)
{
	struct cb_str s;

	s.len = len;
	if (CB_STR_CAN_INLINE(len)) {
		memcpy(s.chars.small, str, len);
	} else {
		s.chars.big = str;
	}

	return s;
}

int cb_str_eq_cstr(cb_str s, const char *cstr, size_t len)
{
	if (len != cb_strlen(s))
		return 0;
	return !strncmp(cb_strptr(&s), cstr, cb_strlen(s));
}

void cb_str_free(cb_str s)
{
	if (!CB_STR_CAN_INLINE(s.len))
		free(s.chars.big);
}

inline uint32_t cb_str_at(cb_str s, size_t idx)
{
	/* FIXME: unicode */
	return cb_strptr(&s)[idx];
}

char *cb_strdup_cstr(struct cb_str str)
{
	char *buf;

	buf = malloc(str.len + 1);
	memcpy(buf, cb_strptr(&str), cb_strlen(str));
	buf[str.len] = 0;

	return buf;
}

struct cb_str cb_strdup(struct cb_str str)
{
	cb_str new_str;

	memcpy(str_init(&new_str, str.len), cb_strptr(&str), str.len);

	return new_str;
}
