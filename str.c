#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utf8proc/utf8proc.h"

#include "cb_util.h"
#include "str.h"

CB_INLINE size_t cb_strlen(cb_str s)
{
	return s.len;
}

CB_INLINE size_t cb_str_ncodepoints(cb_str s)
{
	return s.ncodepoints;
}

ssize_t cb_str_read_char(cb_str s, size_t pos, int32_t *c)
{
	return utf8proc_iterate((unsigned char *) cb_strptr(&s) + pos,
			cb_strlen(s) - pos, c);
}

ssize_t count_codepoints(const char *s, size_t len)
{
	size_t n = 0;
	size_t pos = 0;

	while (pos < len) {
		int32_t c;
		ssize_t result = utf8proc_iterate((unsigned char *) s + pos,
				len - pos, &c);
		if (result < 0)
			return result;
		n++;
		pos += result;
	}

	assert(pos == len);
	return n;
}

char *cb_strptr(cb_str *s)
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
	str->ncodepoints = 0;
	if (!CB_STR_CAN_INLINE(len))
		str->chars.big = malloc(len + 1);
}

static char *str_init(struct cb_str *str, size_t len)
{
	cb_str_init(str, len);
	return cb_strptr(str);
}

ssize_t cb_str_check_utf8(cb_str *str)
{
	ssize_t ncodepoints = count_codepoints(cb_strptr(str), cb_strlen(*str));
	if (ncodepoints < 0)
		return ncodepoints;
	str->ncodepoints = ncodepoints;
	return 0;
}

const char *cb_str_errmsg(ssize_t err)
{
	return utf8proc_errmsg(err);
}

ssize_t cb_str_from_cstr(const char *str, size_t len, cb_str *s)
{
	char *buf;

	buf = str_init(s, len);
	memcpy(buf, str, len);
	buf[len] = 0;

	return cb_str_check_utf8(s);
}

ssize_t cb_str_take_cstr(char *str, size_t len, cb_str *s)
{
	s->len = len;
	if (CB_STR_CAN_INLINE(len)) {
		memcpy(s->chars.small, str, len);
		free(str);
	} else {
		s->chars.big = str;
	}

	return cb_str_check_utf8(s);
}

int cb_strcmp(cb_str a, cb_str b)
{
	if (cb_strlen(a) < cb_strlen(b))
		return -1;
	else if (cb_strlen(a) > cb_strlen(b))
		return 1;
	return memcmp(cb_strptr(&a), cb_strptr(&b), cb_strlen(a));
}

int cb_str_eq_cstr(cb_str s, const char *cstr, size_t len)
{
	if (len != cb_strlen(s))
		return 0;
	return !memcmp(cb_strptr(&s), cstr, cb_strlen(s));
}

void cb_str_free(cb_str s)
{
	if (!CB_STR_CAN_INLINE(s.len))
		free(s.chars.big);
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
	cb_str_check_utf8(&new_str);

	return new_str;
}