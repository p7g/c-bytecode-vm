#include <stdint.h>
#include <time.h>
#include "agent.h"
#include "builtin_modules.h"
#include "module.h"
#include "value.h"

static size_t ident_unix;

void cb_time_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "unix", ident_unix);
}

static int unix_time(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	result->type = CB_VALUE_INT;
	result->val.as_int = (int64_t) time(NULL);
	return 0;
}

void cb_time_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_unix, cb_cfunc_new(ident_unix, 0, unix_time));
}
