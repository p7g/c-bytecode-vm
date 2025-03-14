#include <stdio.h>
#include <stdlib.h>

#include "builtin_modules.h"
#include "error.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

static size_t ident_exit;

void cb_sys_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "exit", ident_exit);
}

static int wrapped_exit(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	exit((int) argv[0].val.as_int);
	return 0;
}

void cb_sys_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_exit,
			cb_cfunc_new(ident_exit, 1, wrapped_exit));
}