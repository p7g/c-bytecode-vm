#include <stdint.h>

#include "builtin_modules.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

static size_t ident_shl, ident_shr;

void cb_math_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "shl", ident_shl);
	CB_DEFINE_EXPORT(spec, "shr", ident_shr);
}

static int left_shift(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[1]);

	result->type = CB_VALUE_INT;
	result->val.as_int = argv[0].val.as_int << argv[1].val.as_int;
	return 0;
}

static int right_shift(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[1]);

	result->type = CB_VALUE_INT;
	result->val.as_int = argv[0].val.as_int >> argv[1].val.as_int;
	return 0;
}

void cb_math_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_shl, cb_cfunc_new(ident_shl, 2, left_shift));
	CB_SET_EXPORT(mod, ident_shr, cb_cfunc_new(ident_shr, 2, right_shift));
}
