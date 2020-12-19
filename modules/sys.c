#include <stdio.h>

#include "builtin_modules.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

size_t ident_panic;

void cb_sys_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "panic", ident_panic);
}

static int panic(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	cb_str msg;

	msg = CB_EXPECT_STRING(argv[0]);
	fprintf(stderr, "%.*s\n", (int) cb_strlen(msg), cb_strptr(msg));
	return 1;
}

void cb_sys_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_panic, cb_cfunc_new(ident_panic, 1, panic));
}
