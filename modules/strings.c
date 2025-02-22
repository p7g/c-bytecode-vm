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
	      ident_next_char,
	      ident_from_bytes,
	      ident_string_iter_result;

static struct cb_struct_spec *get_string_iter_result_spec(void)
{
	static struct cb_struct_spec *spec = NULL;
	if (spec) {
		return spec;
	}

	spec = cb_declare_struct("string_iter_result", 2, "c", "nread");
	return spec;
}

static int next_char(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str strval;
	int64_t intpos;
	size_t pos;

	strval = CB_EXPECT_STRING(argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[1]);
	intpos = argv[1].val.as_int;
	if (intpos < 0 || intpos > INT32_MAX) {
		struct cb_value err;
		(void) cb_value_from_string(&err, "pos is out of bounds");
		cb_error_set(err);
		return 1;
	}

	pos = (size_t) intpos;
	struct cb_value c_val;
	ssize_t nread = 0;
	if (pos >= cb_strlen(strval)) {
		c_val.type = CB_VALUE_NULL;
	} else {
		int32_t c;
		nread = cb_str_read_char(strval, pos, &c);
		c_val = cb_char(c);
	}

	if (nread < 0) {
		struct cb_value err;
		cb_value_from_string(&err, cb_str_errmsg(nread));
		cb_error_set(err);
		return 1;
	}

	result->type = CB_VALUE_STRUCT;
	result->val.as_struct = cb_struct_make(get_string_iter_result_spec(),
			c_val, cb_int(nread));
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
	CB_DEFINE_EXPORT(spec, "from_bytes", ident_from_bytes);
	CB_DEFINE_EXPORT(spec, "next_char", ident_next_char);
	CB_DEFINE_EXPORT(spec, "string_iter_result", ident_string_iter_result);
}

void cb_strings_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_len, cb_cfunc_new(ident_len, 1, len));
	CB_SET_EXPORT(mod, ident_from_bytes,
			cb_cfunc_new(ident_from_bytes, 1, from_bytes));
	CB_SET_EXPORT_FN(mod, ident_next_char, 1, next_char);

	struct cb_value string_iter_result;
	string_iter_result.type = CB_VALUE_STRUCT_SPEC;
	string_iter_result.val.as_struct_spec = get_string_iter_result_spec();
	CB_SET_EXPORT(mod, ident_string_iter_result, string_iter_result);
}