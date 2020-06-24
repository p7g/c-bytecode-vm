#ifndef cb_string_h
#define sb_string_h

#include <stddef.h>
#include <uchar.h>

typedef struct cb_string {
	size_t s_len;
	char32_t *s_chars;
} cb_string;

#endif
