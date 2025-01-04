#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtin_modules.h"
#include "bytes.h"
#include "error.h"
#include "intrinsics.h"
#include "module.h"
#include "str.h"
#include "value.h"

static size_t ident_len,
	      ident_char_at,
	      ident_from_bytes;

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
	if (pos < 0 || cb_strlen(strval) < (size_t) pos) {
		cb_error_set(cb_value_from_string(
					"char_at: String index out of range"));
		return 1;
	}
	result->type = CB_VALUE_CHAR;
	result->val.as_char = cb_str_at(strval, pos);

	return 0;
}

static int from_bytes(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str str;
	struct cb_bytes *bs;

	CB_EXPECT_TYPE(CB_VALUE_BYTES, argv[0]);
	bs = argv[0].val.as_bytes;

	cb_str_init(&str, cb_bytes_len(bs));
	memcpy(cb_strptr(&str), cb_bytes_ptr(bs), cb_bytes_len(bs));

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	result->val.as_string->string = str;
	return 0;
}

void cb_strings_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "len", ident_len);
	CB_DEFINE_EXPORT(spec, "char_at", ident_char_at);
	CB_DEFINE_EXPORT(spec, "from_bytes", ident_from_bytes);
}

void cb_strings_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_len, cb_cfunc_new(ident_len, 1, len));
	CB_SET_EXPORT(mod, ident_char_at,
			cb_cfunc_new(ident_char_at, 1, char_at));
	CB_SET_EXPORT(mod, ident_from_bytes,
			cb_cfunc_new(ident_from_bytes, 1, from_bytes));
}