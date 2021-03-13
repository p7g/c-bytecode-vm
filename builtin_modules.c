#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "builtin_modules.h"
#include "eval.h"
#include "hashmap.h"
#include "module.h"

#include "modules/arrays.h"
#include "modules/errno.h"
#include "modules/fs.h"
#include "modules/time.h"
#include "modules/strings.h"
#include "modules/structs.h"
#include "modules/sys.h"

static const struct cb_builtin_module_spec builtins[] = {
	{"time", cb_time_build_spec, cb_time_instantiate},
	{"structs", cb_structs_build_spec, cb_structs_instantiate},
	{"_fs", cb_fs_build_spec, cb_fs_instantiate},
	{"_string", cb_strings_build_spec, cb_strings_instantiate},
	{"errno", cb_errno_build_spec, cb_errno_instantiate},
	{"sys", cb_sys_build_spec, cb_sys_instantiate},
	{"module", cb_module_build_spec, cb_module_instantiate},
	{"_array", cb_arrays_build_spec, cb_arrays_instantiate},
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
	size_t spec_id, i;
	const struct cb_builtin_module_spec *builtin;
	struct cb_module *mod;
	struct cb_module *modules[cb_builtin_module_count];

	for (i = 0; i < cb_builtin_module_count; i += 1) {
		builtin = &builtins[i];
		spec = cb_agent_get_modspec_by_name(
				cb_agent_intern_string(builtin->name,
					strlen(builtin->name)));
		spec_id = cb_modspec_id(spec);
		mod = &cb_vm_state.modules[spec_id];

		mod->global_scope = cb_hashmap_new();
		mod->spec = spec;
		modules[i] = mod;
	}

	/* run the instantiate functions after populating modules to make sure
	 * all modules are in a valid state. */
	for (i = 0; i < cb_builtin_module_count; i += 1)
		builtins[i].instantiate(modules[i]);
}
