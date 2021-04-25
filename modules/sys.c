#include <stdio.h>
#include <stdlib.h>

#include "builtin_modules.h"
#include "error.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

size_t ident_panic, ident_exit;

void cb_sys_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "panic", ident_panic);
	CB_DEFINE_EXPORT(spec, "exit", ident_exit);
}

static int panic(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	cb_str msg;

	msg = CB_EXPECT_STRING(argv[0]);
	cb_error_set(cb_value_from_fmt("%.*s", (int) cb_strlen(msg),
				cb_strptr(msg)));
	return 1;
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
	CB_SET_EXPORT(mod, ident_panic, cb_cfunc_new(ident_panic, 1, panic));
	CB_SET_EXPORT(mod, ident_exit,
			cb_cfunc_new(ident_exit, 1, wrapped_exit));
}
