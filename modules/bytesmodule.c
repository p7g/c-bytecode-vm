#include "builtin_modules.h"
#include "bytes.h"
#include "error.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

size_t ident_new, ident_copy, ident_get, ident_set, ident_length;

static int bytes_new(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	int64_t len;

	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	len = argv[0].val.as_int;

	if (len < 0) {
		cb_error_set(cb_value_from_string("new: Invalid size"));
		return 1;
	}

	*result = cb_bytes_new_value((size_t) len);
	return 0;
}

static int bytes_copy(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_bytes *from, *to;
	int64_t n;

	CB_EXPECT_TYPE(CB_VALUE_BYTES, argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_BYTES, argv[1]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[2]);

	from = argv[0].val.as_bytes;
	to = argv[1].val.as_bytes;
	n = argv[2].val.as_int;

	if (n < 0) {
		cb_error_set(cb_value_from_string("copy: Invalid amount"));
		return 1;
	}

	if (cb_bytes_copy(from, to, (size_t) n)) {
		cb_error_set(cb_value_from_string(
					"copy: Amount out of bounds"));
		return 1;
	}

	result->type = CB_VALUE_NULL;
	return 0;
}

static int bytes_get(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_bytes *bs;
	int64_t pos;
	int16_t val;

	CB_EXPECT_TYPE(CB_VALUE_BYTES, argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[1]);

	bs = argv[0].val.as_bytes;
	pos = argv[1].val.as_int;

	val = cb_bytes_get(bs, pos);
	if (val < 0) {
		cb_error_set(cb_value_from_string("get: Index out of range"));
		return 1;
	}

	result->type = CB_VALUE_INT;
	result->val.as_int = val;
	return 0;
}

static int bytes_set(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_bytes *bs;
	int64_t pos, val;

	CB_EXPECT_TYPE(CB_VALUE_BYTES, argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[1]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[2]);

	bs = argv[0].val.as_bytes;
	pos = argv[1].val.as_int;
	val = argv[2].val.as_int;

	/* truncate the value to fit */
	if (cb_bytes_set(bs, pos, (uint8_t) val)) {
		cb_error_set(cb_value_from_string("set: Index out of range"));
		return 1;
	}

	result->type = CB_VALUE_NULL;
	return 0;
}

static int bytes_length(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_BYTES, argv[0]);

	result->type = CB_VALUE_INT;
	result->val.as_int = cb_bytes_len(argv[0].val.as_bytes);
	return 0;
}

void cb_bytes_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "new", ident_new);
	CB_DEFINE_EXPORT(spec, "copy", ident_copy);
	CB_DEFINE_EXPORT(spec, "get", ident_get);
	CB_DEFINE_EXPORT(spec, "set", ident_set);
	CB_DEFINE_EXPORT(spec, "length", ident_length);
}

void cb_bytes_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_new, cb_cfunc_new(ident_new, 1, bytes_new));
	CB_SET_EXPORT(mod, ident_copy, cb_cfunc_new(ident_copy, 3, bytes_copy));
	CB_SET_EXPORT(mod, ident_get, cb_cfunc_new(ident_get, 2, bytes_get));
	CB_SET_EXPORT(mod, ident_set, cb_cfunc_new(ident_set, 3, bytes_set));
	CB_SET_EXPORT(mod, ident_length,
			cb_cfunc_new(ident_length, 1, bytes_length));
}
