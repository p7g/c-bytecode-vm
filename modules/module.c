#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtin_modules.h"
#include "error.h"
#include "eval.h"
#include "hashmap.h"
#include "intrinsics.h"
#include "module.h"
#include "str.h"
#include "value.h"

static size_t ident_exports, ident_get, ident_import;

void cb_module_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "exports", ident_exports);
	CB_DEFINE_EXPORT(spec, "get", ident_get);
	CB_DEFINE_EXPORT(spec, "import_", ident_import);
}

static int get_exports(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_array *arr;
	cb_str modname;
	ssize_t id;
	cb_modspec *spec;
	size_t i, num_exports;

	modname = CB_EXPECT_STRING(argv[0]);
	id = cb_agent_get_string_id(cb_strptr(&modname), cb_strlen(modname));

	spec = cb_agent_get_modspec_by_name(id);
	if (!spec) {
		cb_error_set(cb_value_from_fmt("exports: No module '%s'",
				cb_strptr(&modname)));
		return 1;
	}

	num_exports = cb_modspec_n_exports(spec);
	result->type = CB_VALUE_ARRAY;
	arr = result->val.as_array = cb_array_new(num_exports);
	arr->len = num_exports;

	for (i = 0; i < num_exports; i += 1) {
		arr->values[i].type = CB_VALUE_INTERNED_STRING;
		arr->values[i].val.as_interned_string =
				cb_modspec_get_export_name(spec, i);
	}
	return 0;
}

static int get_export(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str modname, export_name;
	ssize_t modname_id, export_name_id;
	cb_modspec *spec;
	struct cb_module *mod;
	struct cb_value *val;

	modname = CB_EXPECT_STRING(argv[0]);
	export_name = CB_EXPECT_STRING(argv[1]);
	modname_id = cb_agent_get_string_id(cb_strptr(&modname),
			cb_strlen(modname));

	spec = cb_agent_get_modspec_by_name(modname_id);
	if (!spec) {
		cb_error_set(cb_value_from_fmt("exports: No module '%s'",
				cb_strptr(&modname)));
		return 1;
	}

	export_name_id = cb_agent_get_string_id(cb_strptr(&export_name),
			cb_strlen(export_name));
	if (export_name_id == -1) {
		cb_error_set(cb_value_from_fmt(
				"get: Module '%s' has no export '%s'",
				cb_strptr(&modname), cb_strptr(&export_name)));
		return 1;
	}

	mod = &cb_vm_state.modules[cb_modspec_id(spec)];
	val = cb_hashmap_get(mod->global_scope, export_name_id);
	if (!val) {
		cb_error_set(cb_value_from_fmt(
				"get: Module '%s' has no export '%s'",
				cb_strptr(&modname), cb_strptr(&export_name)));
		return 1;
	}

	*result = *val;
	return 0;
}

/* This function is pretty sketchy */
static int import(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	cb_str import_name;
	size_t pc;
	FILE *f;
	char *path = NULL;
	int retval = 0;
	struct cb_frame frame;

	import_name = CB_EXPECT_STRING(argv[0]);
	if (argv[1].type != CB_VALUE_NULL) {
		path = cb_strdup_cstr(CB_EXPECT_STRING(argv[1]));
		f = fopen(path, "rb");
		if (!f) {
			perror("import");
			goto err;
		}
	} else {
		/* FIXME: Keep track of current file directory so pwd can be set */
		f = cb_agent_resolve_import(import_name, NULL, &path);
		if (!f)
			goto err;
	}

	pc = cb_bytecode_len(cb_vm_state.bytecode);
	if (cb_compile_module(cb_vm_state.bytecode, import_name, f, path))
		goto err;

	/* Make room in cb_vm_state for new module */
	cb_vm_grow_modules_array(cb_agent_modspec_count());

	frame.is_function = 0;
	frame.module = NULL;
	frame.parent = cb_vm_state.frame;
	frame.bp = cb_vm_state.sp;
	retval = cb_eval(pc, &frame);
	goto end;

err:
	retval = 1;

end:
	if (path)
		free(path);
	return retval;
}

void cb_module_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_exports,
			cb_cfunc_new(ident_exports, 1, get_exports));
	CB_SET_EXPORT(mod, ident_get, cb_cfunc_new(ident_get, 2, get_export));
	CB_SET_EXPORT(mod, ident_import, cb_cfunc_new(ident_import, 2, import));
}