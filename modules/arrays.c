#include "builtin_modules.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

static size_t ident_new, ident_length;

void cb_arrays_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "new", ident_new);
	CB_DEFINE_EXPORT(spec, "length", ident_length);
}

static int array_new(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value len_val, init_val;
	int64_t len;
	len_val = argv[0];

	CB_EXPECT_TYPE(CB_VALUE_INT, len_val);
	len = len_val.val.as_int;
	if (len < 0) {
		cb_error_set(cb_value_from_string("array_new: Invalid length"));
		return 1;
	}

	init_val.type = CB_VALUE_NULL;
	if (argc > 1)
		init_val = argv[1];

	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new((size_t) len);
	result->val.as_array->len = len;

	for (int i = 0; i < len; i += 1)
		result->val.as_array->values[i] = init_val;

	return 0;
}

static int array_length(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_ARRAY, argv[0]);

	result->type = CB_VALUE_INT;
	result->val.as_int = argv[0].val.as_array->len;

	return 0;
}

void cb_arrays_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_new, cb_cfunc_new(ident_new, 1, array_new));
	CB_SET_EXPORT(mod, ident_length,
			cb_cfunc_new(ident_length, 0, array_length));
}