#include <stdio.h>
#include <stdlib.h>

#include "agent.h"
#include "code.h"
#include "constant.h"
#include "struct.h"
#include "value.h"

const char *cb_const_type_name(enum cb_const_type ty)
{
	switch (ty) {
#define CASE(TY) case TY: return #TY;
		CB_CONST_TYPE_LIST(CASE)
#undef CASE
	default:
		fprintf(stderr, "Unknown const type %d\n", ty);
		abort();
	}
}

void cb_const_free(struct cb_const *obj)
{
	switch (obj->type) {
	case CB_CONST_INT:
	case CB_CONST_DOUBLE:
	case CB_CONST_CHAR:
	case CB_CONST_STRING:
	case CB_CONST_STRUCT_SPEC: /* struct spec is GCed */
		break;

	case CB_CONST_ARRAY: {
		struct cb_const_array *arr = obj->val.as_array;
		for (unsigned i = 0; i < arr->len; i += 1)
			cb_const_free(&arr->elements[i]);
		free(arr);
		break;
	}

	case CB_CONST_STRUCT: {
		struct cb_const_struct *struct_ = obj->val.as_struct;
		for (unsigned i = 0; i < struct_->nfields; i += 1)
			cb_const_free(&struct_->fields[i].value);
		free(struct_);
		break;
	}

	case CB_CONST_FUNCTION:
		free(obj->val.as_function);
		break;

	case CB_CONST_MODULE:
		cb_modspec_free(obj->val.as_module->spec);
		free(obj->val.as_module);
		break;

	default:
		fprintf(stderr, "Unknown const type %d\n", obj->type);
		abort();
	}
}

struct cb_value cb_const_to_value(const struct cb_const *const_)
{
	struct cb_value ret;

	switch (const_->type) {
	case CB_CONST_INT:
		ret.type = CB_VALUE_INT;
		ret.val.as_int = const_->val.as_int;
		break;
	case CB_CONST_DOUBLE:
		ret.type = CB_VALUE_DOUBLE;
		ret.val.as_double = const_->val.as_double;
		break;
	case CB_CONST_CHAR:
		ret.type = CB_VALUE_CHAR;
		ret.val.as_char = const_->val.as_char;
		break;
	case CB_CONST_STRING:
		ret.type = CB_VALUE_INTERNED_STRING;
		ret.val.as_interned_string = const_->val.as_string;
		break;
	case CB_CONST_STRUCT_SPEC:
		ret.type = CB_VALUE_STRUCT_SPEC;
		ret.val.as_struct_spec = const_->val.as_struct_spec;
		break;

	case CB_CONST_ARRAY: {
		struct cb_const_array *const_array = const_->val.as_array;
		struct cb_array *array = cb_array_new(const_array->len);
		for (unsigned i = 0; i < const_array->len; i += 1)
			array->values[i] = cb_const_to_value(
					&const_array->elements[i]);
		ret.type = CB_VALUE_ARRAY;
		ret.val.as_array = array;
		break;
	}

	case CB_CONST_STRUCT: {
		struct cb_const_struct *strct = const_->val.as_struct;

		struct cb_struct_spec *spec = cb_struct_spec_new(
				cb_agent_intern_string("<anonymous>", 11),
				strct->nfields);

		/* FIXME: add more efficient way to build struct literal */
		for (unsigned i = 0; i < strct->nfields; i += 1)
			cb_struct_spec_set_field_name(spec, i,
					strct->fields[i].name);

		struct cb_struct *struct_val = cb_struct_spec_instantiate(spec);
		for (unsigned i = 0; i < strct->nfields; i += 1) {
			struct cb_value fieldval = cb_const_to_value(
					&strct->fields[i].value);
			cb_struct_set_field(struct_val, i, fieldval, NULL);
		}

		ret.type = CB_VALUE_STRUCT;
		ret.val.as_struct = struct_val;
		break;
	}

	case CB_CONST_FUNCTION: {
		struct cb_const_user_function *const_func = const_->val
			.as_function;
		struct cb_function *func = cb_function_new();

		func->type = CB_FUNCTION_USER;
		func->name = const_func->name;
		func->arity = const_func->arity;
		func->value.as_user.code = const_func->code;
		if (const_func->code->nupvalues) {
			func->value.as_user.upvalues = malloc(
					const_func->code->nupvalues
					* sizeof(struct cb_upvalue *));
		}
		for (int i = 0; i < const_func->code->nupvalues; i += 1)
			func->value.as_user.upvalues[i] = NULL;
		func->value.as_user.num_opt_params = const_func->num_opt_params;

		ret.type = CB_VALUE_FUNCTION;
		ret.val.as_function = func;
		break;
	}

	case CB_CONST_MODULE:
		fprintf(stderr, "Fatal: Converting const module to value\n");
		exit(1);
		break;

	default:
		fprintf(stderr, "Unknown const type %d\n", const_->type);
		abort();
	}

	return ret;
}