#include <math.h>
#include <stdint.h>

#include "builtin_modules.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

static size_t ident_shl, ident_shr, ident_log, ident_log2, ident_log10;

void cb_math_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "shl", ident_shl);
	CB_DEFINE_EXPORT(spec, "shr", ident_shr);
	CB_DEFINE_EXPORT(spec, "log", ident_log);
	CB_DEFINE_EXPORT(spec, "log2", ident_log2);
	CB_DEFINE_EXPORT(spec, "log10", ident_log10);
}

static int left_shift(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	intptr_t a, b;
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[1]);

	a = argv[0].val.as_int;
	b = argv[1].val.as_int;
	result->type = CB_VALUE_INT;

	if (b < 0) {
		struct cb_value err;
		(void) cb_value_from_fmt(&err, "Shift amount must be positive or 0");
		cb_error_set(err);
		return 1;
	}

	if (a == 0) {
		result->val.as_int = 0;
		return 0;
	}

	int neg = a < 0;
	if (neg)
		a = -a;

	if (b > __builtin_clzl(a) - 1) {
		struct cb_value err;
		(void) cb_value_from_fmt(&err,
				"Left shift of %ld by %ld would overflow", a, b);
		cb_error_set(err);
		return 1;
	} else {
		a <<= b;
	}

	if (neg)
		a = -a;

	result->val.as_int = a;
	return 0;
}

static int right_shift(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	intptr_t a, b;
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[1]);

	a = argv[0].val.as_int;
	b = argv[1].val.as_int;
	result->type = CB_VALUE_INT;

	if (b < 0) {
		struct cb_value err;
		(void) cb_value_from_fmt(&err, "Shift amount must be positive or 0");
		cb_error_set(err);
		return 1;
	}

	int neg = a < 0;
	if (neg)
		a = ~a;

	a >>= b;

	if (neg)
		a = ~a;

	result->val.as_int = a;
	return 0;
}

static int log_(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_DOUBLE, argv[0]);

	result->type = CB_VALUE_DOUBLE;
	result->val.as_double = log(argv[0].val.as_double);

	return 0;
}

static int log10_(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_DOUBLE, argv[0]);

	result->type = CB_VALUE_DOUBLE;
	result->val.as_double = log10(argv[0].val.as_double);

	return 0;
}

static int log2_(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_DOUBLE, argv[0]);

	result->type = CB_VALUE_DOUBLE;
	result->val.as_double = log2(argv[0].val.as_double);

	return 0;
}

void cb_math_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_shl, cb_cfunc_new(ident_shl, 2, left_shift));
	CB_SET_EXPORT(mod, ident_shr, cb_cfunc_new(ident_shr, 2, right_shift));
	CB_SET_EXPORT(mod, ident_log, cb_cfunc_new(ident_log, 1, log_));
	CB_SET_EXPORT(mod, ident_log2, cb_cfunc_new(ident_log2, 1, log2_));
	CB_SET_EXPORT(mod, ident_log10, cb_cfunc_new(ident_log10, 1, log10_));
}