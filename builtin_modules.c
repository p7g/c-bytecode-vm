#include <assert.h>
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
#include "modules/chars.h"
#include "modules/errno.h"
#include "modules/fs.h"
#include "modules/inet.h"
#include "modules/math.h"
#include "modules/time.h"
#include "modules/socket.h"
#include "modules/strings.h"
#include "modules/structs.h"
#include "modules/sys.h"

#define BUILTIN_MODULES(X) \
	X(time, time) \
	X(structs, structs) \
	X(fs, _fs) \
	X(strings, _string) \
	X(errno, errno) \
	X(sys, sys) \
	X(module, _module) \
	X(arrays, _array) \
	X(inet, _inet) \
	X(math, _math) \
	X(bytes, _bytes) \
	X(socket, _socket) \
	X(chars, _char)

#define MOD(NAME, PUB_NAME) { \
		.name = #PUB_NAME, \
		.name_len = sizeof(#PUB_NAME) - 1, \
		.build_spec = cb_ ## NAME ## _build_spec, \
		.instantiate = cb_ ## NAME ## _instantiate \
	},
static struct cb_builtin_module_spec builtins[] = {
	BUILTIN_MODULES(MOD)
};
#undef MOD

const struct cb_builtin_module_spec *cb_builtin_modules = builtins;
const size_t cb_builtin_module_count = sizeof(builtins) / sizeof(builtins[0]);

void cb_initialize_builtin_modules(void)
{
	cb_modspec *modspec;
	size_t name;

	for (size_t i = 0; i < cb_builtin_module_count; i += 1) {
		name = cb_agent_intern_string((const char *) builtins[i].name,
					builtins[i].name_len);
		builtins[i].name_id = name;
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
		spec = cb_agent_get_modspec_by_name(builtin->name_id);
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
			nfields, 0);

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

static struct cb_upvalue *cfunc_get_upvalue(size_t i)
{
	struct cb_frame *frame;
	struct cb_value funcval;
	struct cb_function *func;

	frame = cb_vm_state.frame;
	funcval = cb_vm_state.stack[frame->bp];
	assert(funcval.type == CB_VALUE_FUNCTION);
	func = funcval.val.as_function;
	assert(func->type == CB_FUNCTION_NATIVE);
	assert(i < func->nupvalues);

	return func->upvalues[i];
}

struct cb_value cb_cfunc_load_upvalue(size_t i)
{
	return cb_load_upvalue(cfunc_get_upvalue(i));
}

void cb_cfunc_store_upvalue(size_t i, struct cb_value val)
{
	cb_store_upvalue(cfunc_get_upvalue(i), val);
}