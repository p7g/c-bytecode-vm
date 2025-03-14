#include <stdio.h>
#include <stdlib.h>

#include "agent.h"
#include "builtin_modules.h"
#include "error.h"
#include "intrinsics.h"
#include "module.h"
#include "str.h"
#include "struct.h"
#include "value.h"

static size_t ident_new, ident_fields, ident_spec,
		ident_name, ident_get, ident_set, ident_get_method;

void cb_structs_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "new", ident_new);
	CB_DEFINE_EXPORT(spec, "fields", ident_fields);
	CB_DEFINE_EXPORT(spec, "spec", ident_spec);
	CB_DEFINE_EXPORT(spec, "name", ident_name);
	CB_DEFINE_EXPORT(spec, "get", ident_get);
	CB_DEFINE_EXPORT(spec, "set", ident_set);
	CB_DEFINE_EXPORT(spec, "get_method", ident_get_method);
}

/* TODO: support methods */
static int make_struct_spec(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str name;
	struct cb_array *fields;
	size_t i;

	name = CB_EXPECT_STRING(argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_ARRAY, argv[1]);
	fields = argv[1].val.as_array;

	result->type = CB_VALUE_STRUCT_SPEC;
	result->val.as_struct_spec = cb_struct_spec_new(
			cb_agent_intern_string(cb_strptr(&name),
				cb_strlen(name)),
			fields->len, 0);

	for (i = 0; i < fields->len; i += 1) {
		name = CB_EXPECT_STRING(fields->values[i]);
		cb_struct_spec_set_field_name(result->val.as_struct_spec, i,
				cb_agent_intern_string(cb_strptr(&name),
					cb_strlen(name)));
	}

	return 0;
}

static int get_spec_fields(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_struct_spec *spec;
	struct cb_value *el;
	size_t i;

	CB_EXPECT_TYPE(CB_VALUE_STRUCT_SPEC, argv[0]);

	spec = argv[0].val.as_struct_spec;
	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new(spec->nfields);
	result->val.as_array->len = spec->nfields;

	for (i = 0; i < spec->nfields; i += 1) {
		el = &result->val.as_array->values[i];
		el->type = CB_VALUE_INTERNED_STRING;
		el->val.as_interned_string = spec->fields[i];
	}

	return 0;
}

static int get_struct_spec(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_STRUCT, argv[0]);
	result->type = CB_VALUE_STRUCT_SPEC;
	result->val.as_struct_spec = argv[0].val.as_struct->spec;
	return 0;
}

static int get_spec_name(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_STRUCT_SPEC, argv[0]);
	result->type = CB_VALUE_INTERNED_STRING;
	result->val.as_interned_string = argv[0].val.as_struct_spec->name;
	return 0;
}

static int get_struct_field(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str fname;
	struct cb_value *fvalue;
	cb_str as_str;

	CB_EXPECT_TYPE(CB_VALUE_STRUCT, argv[0]);
	fname = CB_EXPECT_STRING(argv[1]);
	fvalue = cb_struct_get_field(argv[0].val.as_struct,
			cb_agent_intern_string(cb_strptr(&fname),
				cb_strlen(fname)), NULL);
	if (fvalue == NULL) {
		as_str = cb_value_to_string(argv[0]);
		struct cb_value err;
		cb_value_from_fmt(&err, "Value '%s' has no field '%s'",
				cb_strptr(&as_str), cb_strptr(&fname));
		cb_error_set(err);
		cb_str_free(as_str);
		return 1;
	}

	*result = *fvalue;
	return 0;
}

static int set_struct_field(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str fname;
	cb_str as_str;
	size_t fname_id;
	int retval;

	CB_EXPECT_TYPE(CB_VALUE_STRUCT, argv[0]);
	fname = CB_EXPECT_STRING(argv[1]);
	fname_id = cb_agent_intern_string(cb_strptr(&fname), cb_strlen(fname));
	retval = cb_struct_set_field(argv[0].val.as_struct, fname_id, argv[2],
			NULL);
	if (retval) {
		as_str = cb_value_to_string(argv[0]);
		struct cb_value err;
		cb_value_from_fmt(&err, "Value '%s' has no field '%s'",
				cb_strptr(&as_str), cb_strptr(&fname));
		cb_error_set(err);
		cb_str_free(as_str);
		return 1;
	}

	result->type = CB_VALUE_NULL;
	return 0;
}

static int get_method(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_struct_spec *spec;
	cb_str method_name;

	if (argv[0].type == CB_VALUE_STRUCT_SPEC) {
		spec = argv[0].val.as_struct_spec;
	} else if (argv[0].type == CB_VALUE_STRUCT) {
		spec = argv[0].val.as_struct->spec;
	} else {
		CB_EXPECT_TYPE(CB_VALUE_STRUCT, argv[0]);
	}
	method_name = CB_EXPECT_STRING(argv[1]);

	unsigned i;
	for (i = 0; i < spec->nmethods; i++) {
		struct cb_method *method = &spec->methods[i];
		cb_str current_method_name = cb_agent_get_string(method->name);
		if (!cb_strcmp(current_method_name, method_name)) {
			break;
		}
	}

	if (i == spec->nmethods) {
		struct cb_value err;
		cb_str as_str = cb_value_to_string(argv[0]);
		cb_value_from_fmt(&err, "Value '%s' has no method '%s'",
				cb_strptr(&as_str), cb_strptr(&method_name));
		cb_error_set(err);
		cb_str_free(as_str);
		return 1;
	}

	result->type = CB_VALUE_FUNCTION;
	result->val.as_function = spec->methods[i].func;

	return 0;
}

void cb_structs_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_new, cb_cfunc_new(ident_new, 2, make_struct_spec));
	CB_SET_EXPORT(mod, ident_fields,
			cb_cfunc_new(ident_fields, 1, get_spec_fields));
	CB_SET_EXPORT(mod, ident_spec,
			cb_cfunc_new(ident_spec, 1, get_struct_spec));
	CB_SET_EXPORT(mod, ident_name,
			cb_cfunc_new(ident_name, 1, get_spec_name));
	CB_SET_EXPORT(mod, ident_get,
			cb_cfunc_new(ident_get, 2, get_struct_field));
	CB_SET_EXPORT(mod, ident_set,
			cb_cfunc_new(ident_set, 3, set_struct_field));
	CB_SET_EXPORT_FN(mod, ident_get_method, 2, get_method);
}