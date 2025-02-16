#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtin_modules.h"
#include "code.h"
#include "disassemble.h"
#include "error.h"
#include "eval.h"
#include "hashmap.h"
#include "intrinsics.h"
#include "module.h"
#include "str.h"
#include "value.h"

static size_t ident_export_names, ident_get, ident_import;

void cb_module_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "export_names", ident_export_names);
	CB_DEFINE_EXPORT(spec, "get", ident_get);
	CB_DEFINE_EXPORT(spec, "import_", ident_import);
}

static int get_export_names(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_array *arr;
	cb_str modname;
	ssize_t id;
	cb_modspec *spec;
	size_t i, num_exports;

	modname = CB_EXPECT_STRING(argv[0]);
	id = cb_agent_get_string_id(cb_strptr(&modname), cb_strlen(modname));

	if (id == -1 || !(spec = cb_agent_get_modspec_by_name(id))) {
		struct cb_value err;
		cb_value_from_fmt(&err, "export_names: No module '%s'",
				cb_strptr(&modname));
		cb_error_set(err);
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

	modname = CB_EXPECT_STRING(argv[0]);
	export_name = CB_EXPECT_STRING(argv[1]);
	modname_id = cb_agent_get_string_id(cb_strptr(&modname),
			cb_strlen(modname));

	spec = cb_agent_get_modspec_by_name(modname_id);
	if (!spec) {
		struct cb_value err;
		cb_value_from_fmt(&err, "get: No module '%s'", cb_strptr(&modname));
		cb_error_set(err);
		return 1;
	}

	export_name_id = cb_agent_get_string_id(cb_strptr(&export_name),
			cb_strlen(export_name));
	if (export_name_id == -1) {
		struct cb_value err;
		cb_value_from_fmt(&err,
				"get: Module '%s' has no export '%s'",
				cb_strptr(&modname), cb_strptr(&export_name));
		cb_error_set(err);
		return 1;
	}

	mod = &cb_vm_state.modules[cb_modspec_id(spec)];
	assert(mod->global_scope);
	if (!cb_hashmap_get(mod->global_scope, export_name_id, result)) {
		struct cb_value err;
		cb_value_from_fmt(&err,
				"get: Module '%s' has no export '%s'",
				cb_strptr(&modname), cb_strptr(&export_name));
		cb_error_set(err);
		return 1;
	}

	return 0;
}

static int import(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	cb_str import_name_str;
	size_t import_name;
	FILE *f;
	char *path = NULL;
	int retval = 0;
	cb_modspec *modspec;

	import_name_str = CB_EXPECT_STRING(argv[0]);
	if (argv[1].type != CB_VALUE_NULL) {
		path = cb_strdup_cstr(CB_EXPECT_STRING(argv[1]));
		f = fopen(path, "rb");
		if (!f) {
			cb_error_from_errno();
			goto err;
		}
	} else {
		/* FIXME: Keep track of current file directory so pwd can be set */
		f = cb_agent_resolve_import(import_name_str, NULL, &path);
		if (!f) {
			goto err;
		}
	}

	import_name = cb_agent_intern_string(cb_strptr(&import_name_str),
			cb_strlen(import_name_str));
	modspec = cb_agent_get_modspec_by_name(import_name);
	if (!modspec) {
		modspec = cb_modspec_new(import_name);
		cb_agent_add_modspec(modspec);
	}

	if (cb_compile_file(modspec, f)) {
		struct cb_value err;
		cb_value_from_string(&err, "Compile error");
		cb_error_set(err);
		goto err;
	}

	retval = cb_run(cb_modspec_code(modspec));

	goto end;

err:
	retval = 1;

end:
	if (path)
		free(path);

	result->type = CB_VALUE_NULL;
	return retval;
}

void cb_module_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_export_names,
			cb_cfunc_new(ident_export_names, 1, get_export_names));
	CB_SET_EXPORT(mod, ident_get, cb_cfunc_new(ident_get, 2, get_export));
	CB_SET_EXPORT(mod, ident_import, cb_cfunc_new(ident_import, 2, import));
}