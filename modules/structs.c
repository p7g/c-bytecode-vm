#include <stdio.h>
#include <stdlib.h>

#include "agent.h"
#include "intrinsics.h"
#include "module.h"
#include "str.h"
#include "struct.h"
#include "value.h"

static size_t ident_new, ident_fields, ident_spec,
		ident_name, ident_get, ident_set;

#define DEFINE_EXPORT(SPEC, NAME, VAR) \
	cb_modspec_add_export((SPEC), \
		((VAR) = cb_agent_intern_string((NAME), sizeof((NAME)) - 1)))
#define SET_EXPORT(MOD, NAME, VAL) \
	cb_hashmap_set((MOD)->global_scope, (NAME), (VAL));

void cb_structs_build_spec(cb_modspec *spec)
{
	DEFINE_EXPORT(spec, "new", ident_new);
	DEFINE_EXPORT(spec, "fields", ident_fields);
	DEFINE_EXPORT(spec, "spec", ident_spec);
	DEFINE_EXPORT(spec, "name", ident_name);
	DEFINE_EXPORT(spec, "get", ident_get);
	DEFINE_EXPORT(spec, "set", ident_set);
}

static int make_struct_spec(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str name;
	size_t i;

	name = CB_EXPECT_STRING(argv[0]);

	result->type = CB_VALUE_STRUCT_SPEC;
	result->val.as_struct_spec = cb_struct_spec_new(
			cb_agent_intern_string(cb_strptr(name), cb_strlen(name)),
			argc - 1);

	for (i = 1; i < argc; i += 1) {
		name = CB_EXPECT_STRING(argv[i]);
		cb_struct_spec_set_field_name(result->val.as_struct_spec, i - 1,
				cb_agent_intern_string(cb_strptr(name),
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
	char *as_str;

	CB_EXPECT_TYPE(CB_VALUE_STRUCT, argv[0]);
	fname = CB_EXPECT_STRING(argv[1]);
	fvalue = cb_struct_get_field(argv[0].val.as_struct,
			cb_agent_intern_string(cb_strptr(fname),
				cb_strlen(fname)));
	if (fvalue == NULL) {
		fprintf(stderr, "Value '%s' has no field '%s'\n",
				(as_str = cb_value_to_string(&argv[0])),
				cb_strptr(fname));
		free(as_str);
		return 1;
	}

	*result = *fvalue;
	return 0;
}

static int set_struct_field(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str fname;
	char *as_str;
	size_t fname_id;
	int retval;

	CB_EXPECT_TYPE(CB_VALUE_STRUCT, argv[0]);
	fname = CB_EXPECT_STRING(argv[1]);
	fname_id = cb_agent_intern_string(cb_strptr(fname), cb_strlen(fname));
	retval = cb_struct_set_field(argv[0].val.as_struct, fname_id, argv[2]);
	if (retval) {
		fprintf(stderr, "Value '%s' has no field '%s'\n",
				(as_str = cb_value_to_string(&argv[0])),
				cb_strptr(fname));
		free(as_str);
		return 1;
	}

	result->type = CB_VALUE_NULL;
	return 0;
}

void cb_structs_instantiate(struct cb_module *mod)
{
	SET_EXPORT(mod, ident_new, cb_cfunc_new(ident_new, 1, make_struct_spec));
	SET_EXPORT(mod, ident_fields,
			cb_cfunc_new(ident_fields, 1, get_spec_fields));
	SET_EXPORT(mod, ident_spec,
			cb_cfunc_new(ident_spec, 1, get_struct_spec));
	SET_EXPORT(mod, ident_name,
			cb_cfunc_new(ident_name, 1, get_spec_name));
	SET_EXPORT(mod, ident_get,
			cb_cfunc_new(ident_get, 2, get_struct_field));
	SET_EXPORT(mod, ident_set,
			cb_cfunc_new(ident_set, 3, set_struct_field));
}
