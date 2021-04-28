#include <stdio.h>
#include <stdlib.h>

#include "builtin_modules.h"
#include "bytes.h"
#include "error.h"
#include "intrinsics.h"
#include "module.h"
#include "str.h"
#include "value.h"

size_t ident_len,
       ident_char_at;

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
		cb_error_set(cb_value_from_string(
					"char_at: String index out of range"));
		return 1;
	}
	result->type = CB_VALUE_CHAR;
	result->val.as_char = cb_str_at(strval, pos);

	return 0;
}

void cb_strings_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "len", ident_len);
	CB_DEFINE_EXPORT(spec, "char_at", ident_char_at);
}

void cb_strings_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_len, cb_cfunc_new(ident_len, 1, len));
	CB_SET_EXPORT(mod, ident_char_at,
			cb_cfunc_new(ident_char_at, 1, char_at));
}