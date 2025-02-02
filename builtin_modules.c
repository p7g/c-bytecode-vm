#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "builtin_modules.h"
#include "eval.h"
#include "hashmap.h"
#include "module.h"
#include "struct.h"
#include "value.h"

#include "modules/arrays.h"
#include "modules/bytesmodule.h"
#include "modules/errno.h"
#include "modules/fs.h"
#include "modules/inet.h"
#include "modules/math.h"
#include "modules/time.h"
#include "modules/strings.h"
#include "modules/structs.h"
#include "modules/sys.h"

/* TODO: intern module names to make comparison easier later */
static const struct cb_builtin_module_spec builtins[] = {
	{"time", cb_time_build_spec, cb_time_instantiate},
	{"structs", cb_structs_build_spec, cb_structs_instantiate},
	{"_fs", cb_fs_build_spec, cb_fs_instantiate},
	{"_string", cb_strings_build_spec, cb_strings_instantiate},
	{"errno", cb_errno_build_spec, cb_errno_instantiate},
	{"sys", cb_sys_build_spec, cb_sys_instantiate},
	{"module", cb_module_build_spec, cb_module_instantiate},
	{"_array", cb_arrays_build_spec, cb_arrays_instantiate},
	{"_inet", cb_inet_build_spec, cb_inet_instantiate},
	{"_math", cb_math_build_spec, cb_math_instantiate},
	{"_bytes", cb_bytes_build_spec, cb_bytes_instantiate},
};
const struct cb_builtin_module_spec *cb_builtin_modules = builtins;
const size_t cb_builtin_module_count = sizeof(builtins) / sizeof(builtins[0]);

void cb_initialize_builtin_modules(void)
{
	cb_modspec *modspec;
	size_t name;

	for (size_t i = 0; i < cb_builtin_module_count; i += 1) {
		name = cb_agent_intern_string(builtins[i].name,
					strlen(builtins[i].name));
		modspec = cb_modspec_new(name);

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

		mod->spec = spec;
		modules[i] = mod;
	}

	/* run the instantiate functions after populating modules to make sure
	 * all modules are in a valid state. */
	for (i = 0; i < cb_builtin_module_count; i += 1)
		builtins[i].instantiate(modules[i]);
}

struct cb_struct_spec *cb_declare_struct(const char *name, unsigned nfields,
		...)
{
	va_list args;
	va_start(args, nfields);

	struct cb_struct_spec *spec = cb_struct_spec_new(
			cb_agent_intern_string(name, strlen(name)),
			nfields);

	for (unsigned i = 0; i < nfields; i++) {
		const char *fieldname = va_arg(args, const char *);
		size_t fieldname_id = cb_agent_intern_string(fieldname,
				strlen(fieldname));
		cb_struct_spec_set_field_name(spec, i, fieldname_id);
	}

	va_end(args);
	return spec;
}

struct cb_struct *cb_struct_make(struct cb_struct_spec *spec, ...)
{
	va_list args;
	va_start(args, spec);

	struct cb_struct *s = cb_struct_spec_instantiate(spec);

	for (unsigned i = 0; i < spec->nfields; i++)
		s->fields[i] = va_arg(args, struct cb_value);

	va_end(args);
	return s;
}