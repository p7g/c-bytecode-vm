#include <stdio.h>
#include <stdlib.h>

#include "builtin_modules.h"
#include "intrinsics.h"
#include "module.h"
#include "str.h"
#include "value.h"

size_t ident_buf, ident_resize_buf, ident_len, ident_char_at;

static int make_buf(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	size_t len;

	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	if (argv[0].val.as_int < 0) {
		fprintf(stderr, "buf: Invalid buf length\n");
		return 1;
	}

	len = argv[0].val.as_int;
	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	result->val.as_string->string.len = len;
	result->val.as_string->string.chars = calloc(len, sizeof(char));
	return 0;
}

static int resize_buf(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	size_t len;
	cb_str *str;

	CB_EXPECT_TYPE(CB_VALUE_STRING, argv[0]); /* not interned */
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[1]);
	str = &argv[0].val.as_string->string;
	len = argv[1].val.as_int;
	if (len < 0) {
		fprintf(stderr, "resize_buf: Invalid buf length\n");
		return 1;
	}

	str->len = len;
	str->chars = realloc(str->chars, sizeof(char) * len);
	result->type = CB_VALUE_NULL;
	return 0;
}

static int len(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	cb_str strval;

	strval = CB_EXPECT_STRING(argv[0]);
	result->type = CB_VALUE_INT;
	result->val.as_int = cb_strlen(strval);

	return 0;
}

static int char_at(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	cb_str strval;
	int64_t pos;

	strval = CB_EXPECT_STRING(argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[1]);
	pos = argv[1].val.as_int;
	if (pos < 0 || cb_strlen(strval) < pos) {
		fprintf(stderr, "char_at: String index out of range\n");
		return 1;
	}
	result->type = CB_VALUE_CHAR;
	result->val.as_char = cb_str_at(strval, pos);

	return 0;
}

void cb_strings_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "buf", ident_buf);
	CB_DEFINE_EXPORT(spec, "resize_buf", ident_resize_buf);
	CB_DEFINE_EXPORT(spec, "len", ident_len);
	CB_DEFINE_EXPORT(spec, "char_at", ident_char_at);
}

void cb_strings_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_buf, cb_cfunc_new(ident_buf, 1, make_buf));
	CB_SET_EXPORT(mod, ident_resize_buf,
			cb_cfunc_new(ident_resize_buf, 2, resize_buf));
	CB_SET_EXPORT(mod, ident_len, cb_cfunc_new(ident_len, 1, len));
	CB_SET_EXPORT(mod, ident_char_at,
			cb_cfunc_new(ident_char_at, 1, char_at));
}