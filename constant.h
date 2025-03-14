#ifndef cb_constant_h
#define cb_constant_h

#include "module.h"
#include "value.h"

struct cb_code;

#define CB_CONST_TYPE_LIST(X) \
	X(CB_CONST_INT) \
	X(CB_CONST_DOUBLE) \
	X(CB_CONST_CHAR) \
	X(CB_CONST_STRING) \
	X(CB_CONST_ARRAY) \
	X(CB_CONST_STRUCT) \
	X(CB_CONST_STRUCT_SPEC) \
	X(CB_CONST_FUNCTION) \
	X(CB_CONST_MODULE)

enum cb_const_type {
#define VARIANT(V) V,
	CB_CONST_TYPE_LIST(VARIANT)
#undef VARIANT
};

struct cb_const_array;
struct cb_const_struct;
struct cb_const_user_function;

struct cb_const {
	enum cb_const_type type;
	union {
		intptr_t as_int;
		double as_double;
		int32_t as_char;
		size_t as_string;  /* always interned */
		struct cb_const_array *as_array;
		struct cb_const_struct *as_struct;
		struct cb_struct_spec *as_struct_spec;
		struct cb_const_user_function *as_function;
		cb_modspec *as_module;
	} val;
};

struct cb_const_array {
	size_t len;
	struct cb_const elements[];
};

struct cb_const_struct_field {
	size_t name;
	struct cb_const value;
};

struct cb_const_struct {
	size_t nfields;
	struct cb_const_struct_field fields[];
};

struct cb_const_user_function {
	size_t name, arity;
	struct cb_code *code;
};

const char *cb_const_type_name(enum cb_const_type);
void cb_const_free(struct cb_const *obj);
struct cb_value cb_const_to_value(const struct cb_const *const_);

#endif