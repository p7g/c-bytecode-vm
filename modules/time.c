#include <stdint.h>
#include <time.h>
#include "agent.h"
#include "module.h"
#include "value.h"

static size_t ident_unix;

void cb_time_build_spec(cb_modspec *spec)
{
	cb_modspec_add_export(spec,
			(ident_unix = cb_agent_intern_string("unix", 4)));
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
	cb_hashmap_set(mod->global_scope, ident_unix,
			cb_cfunc_new(ident_unix, 0, unix_time));
}
