#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "builtin_modules.h"
#include "eval.h"
#include "hashmap.h"
#include "module.h"

#include "modules/time.h"

static const struct cb_builtin_module_spec builtins[] = {
	{"time", cb_time_build_spec, cb_time_instantiate},
};
const struct cb_builtin_module_spec *cb_builtin_modules = builtins;
const size_t cb_builtin_module_count = sizeof(builtins) / sizeof(builtins[0]);

void cb_initialize_builtin_modules(void)
{
	cb_modspec *modspec;

	for (size_t i = 0; i < cb_builtin_module_count; i += 1) {
		modspec = cb_modspec_new(cb_agent_intern_string(
					builtins[i].name,
					strlen(builtins[i].name)));

		builtins[i].build_spec(modspec);
		cb_agent_add_modspec(modspec);
	}
}

void cb_instantiate_builtin_modules(void)
{
	cb_modspec *spec;
	size_t spec_id;
	const struct cb_builtin_module_spec *builtin;
	struct cb_module *mod;

	for (size_t i = 0; i < cb_builtin_module_count; i += 1) {
		builtin = &builtins[i];
		spec = cb_agent_get_modspec_by_name(
				cb_agent_intern_string(builtin->name,
					strlen(builtin->name)));
		spec_id = cb_modspec_id(spec);
		mod = &cb_vm_state.modules[spec_id];

		mod->global_scope = cb_hashmap_new();
		mod->spec = spec;
		builtin->instantiate(mod);
	}
}
