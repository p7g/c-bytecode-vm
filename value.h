#ifndef cb_value_h
#define cb_value_h

#include <stdint.h>

#include "gc.h"
#include "string.h"

#define CB_VALUE_IS_USER_FN(V) ((V)->type == CB_VALUE_FUNCTION \
		&& (V)->val.as_function->type == CB_FUNCTION_USER)

#define CB_VALUE_TYPE_LIST(X) \
	X(CB_VALUE_INT) \
	X(CB_VALUE_DOUBLE) \
	X(CB_VALUE_BOOL) \
	X(CB_VALUE_NULL) \
	X(CB_VALUE_CHAR) \
	X(CB_VALUE_INTERNED_STRING) \
	X(CB_VALUE_STRING) \
	X(CB_VALUE_ARRAY) \
	X(CB_VALUE_FUNCTION)

enum cb_value_type {
#define COMMA(V) V,
	CB_VALUE_TYPE_LIST(COMMA)
#undef COMMA
};

struct cb_value;

struct cb_string {
	cb_gc_header gc_header;
	cb_str string;
};

enum cb_function_type {
	CB_FUNCTION_NATIVE,
	CB_FUNCTION_USER,
};

/* FIXME: argv should be const since it points to the stack */
typedef int (cb_native_function)(size_t argc, struct cb_value *argv,
		struct cb_value *retval);

struct cb_user_function {
	size_t address;
	size_t *upvalues;
	size_t upvalues_size, upvalues_len;
};

struct cb_function {
	cb_gc_header gc_header;
	enum cb_function_type type;
	size_t name, arity;
	union {
		cb_native_function *as_native;
		struct cb_user_function as_user;
	} value;
};

struct cb_array;

struct cb_value {
	enum cb_value_type type;
	union {
		intptr_t as_int;
		double as_double;
		int as_bool;
		uint32_t as_char;
		size_t as_interned_string;
		/* heap allocated */
		struct cb_string *as_string;
		struct cb_array *as_array;
		struct cb_function *as_function;
	} val;
};

struct cb_array {
	cb_gc_header gc_header;
	size_t len;
	struct cb_value values[];
};

int cb_value_eq(struct cb_value *a, struct cb_value *b);
int cb_value_cmp(struct cb_value *a, struct cb_value *b, int *ok);
char *cb_value_to_string(struct cb_value *val);
int cb_value_is_truthy(struct cb_value *val);
void cb_function_add_upvalue(struct cb_user_function *fn, size_t idx);

void cb_value_mark(struct cb_value *val);
void cb_value_incref(struct cb_value *val);
void cb_value_decref(struct cb_value *val);
struct cb_function *cb_function_new(void);
struct cb_string *cb_string_new(void);
struct cb_array *cb_array_new(size_t len);

const char *cb_value_type_name(enum cb_value_type type);

#endif
