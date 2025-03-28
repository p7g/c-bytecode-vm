#ifndef cb_value_h
#define cb_value_h

#include <stdint.h>

#include "gc.h"
#include "str.h"
#include "userdata.h"

#define CB_VALUE_IS_USER_FN(V) ((V)->type == CB_VALUE_FUNCTION \
		&& (V)->val.as_function->type == CB_FUNCTION_USER)

#define CB_MAX_PARAMS 32

/* It's important that CB_VALUE_NULL is first (i.e. 0). In DO_OP_ALLOCATE_LOCALS
 * we memset the stack to 0, which is enabled by this. */
#define CB_VALUE_TYPE_LIST(X) \
	X(CB_VALUE_NULL) \
	X(CB_VALUE_INT) \
	X(CB_VALUE_DOUBLE) \
	X(CB_VALUE_BOOL) \
	X(CB_VALUE_CHAR) \
	X(CB_VALUE_INTERNED_STRING) \
	X(CB_VALUE_STRING) \
	X(CB_VALUE_BYTES) \
	X(CB_VALUE_ARRAY) \
	X(CB_VALUE_FUNCTION) \
	X(CB_VALUE_STRUCT_SPEC) \
	X(CB_VALUE_STRUCT) \
	X(CB_VALUE_USERDATA)

enum cb_value_type {
#define COMMA(V) V,
	CB_VALUE_TYPE_LIST(COMMA)
#undef COMMA
};

struct cb_upvalue;
struct cb_value;

struct cb_string {
	cb_gc_header gc_header;
	cb_str string;
};

struct cb_bytes {
	cb_gc_header gc_header;
	uint8_t data[];
};

enum cb_function_type {
	CB_FUNCTION_NATIVE,
	CB_FUNCTION_USER,
};

/* FIXME: argv should be const since it points to the stack */
typedef int (cb_native_function)(size_t argc, struct cb_value *argv,
		struct cb_value *retval);

struct cb_code;

struct cb_user_function {
	struct cb_code *code;
};

struct cb_function {
	cb_gc_header gc_header;
	enum cb_function_type type;
	size_t name, arity;
	union {
		cb_native_function *as_native;
		struct cb_user_function as_user;
	} value;
	/* nupvalues is only set for native functions; for user functions use
	   value.as_user->code.nupvalues */
	size_t nupvalues;
	struct cb_upvalue **upvalues;
};

struct cb_array;

struct cb_value {
	enum cb_value_type type;
	union {
		intptr_t as_int;
		double as_double;
		int as_bool;
		int32_t as_char;
		size_t as_interned_string;
		/* heap allocated */
		struct cb_string *as_string;
		struct cb_array *as_array;
		struct cb_function *as_function;
		struct cb_struct *as_struct;
		struct cb_struct_spec *as_struct_spec;
		struct cb_userdata *as_userdata;
		struct cb_bytes *as_bytes;
	} val;
};

struct cb_array {
	cb_gc_header gc_header;
	size_t len;
	struct cb_value values[];
};

int cb_value_eq(struct cb_value *a, struct cb_value *b);
double cb_value_cmp(struct cb_value *a, struct cb_value *b, int *ok);
cb_str cb_value_to_string(struct cb_value val);
int cb_value_is_truthy(struct cb_value *val);
void cb_function_add_upvalue(struct cb_function *fn, size_t idx,
		struct cb_upvalue *uv);
int cb_value_call(struct cb_value fn, struct cb_value *args, size_t args_len,
		struct cb_value *result);

cb_gc_hold_key *cb_value_gc_hold(struct cb_value *val);
void cb_value_mark(struct cb_value val);
void cb_value_incref(struct cb_value val);
void cb_value_decref(struct cb_value val);
struct cb_function *cb_function_new(void);
void cb_function_mark(struct cb_function *func);
struct cb_string *cb_string_new(void);
struct cb_array *cb_array_new(size_t len);
cb_gc_hold_key *cb_array_gc_hold(struct cb_array *arr);
struct cb_value cb_cfunc_new(size_t name, size_t arity,
		cb_native_function *func);
struct cb_value cb_int(int64_t);
struct cb_value cb_double(double);
struct cb_value cb_bool(int);
struct cb_value cb_char(int32_t);
ssize_t cb_value_from_string(struct cb_value *val, const char *str);
ssize_t cb_value_from_fmt(struct cb_value *val, const char *fmt, ...);
struct cb_bytes *cb_bytes_new(size_t size);
struct cb_value cb_bytes_new_value(size_t size);
size_t cb_function_upvalue_count(const struct cb_function *func);
int cb_value_id(struct cb_value value, intptr_t *id_out);

const char *cb_value_type_name(enum cb_value_type type);
const char *cb_value_type_of(struct cb_value *val);
const char *cb_value_type_friendly_name(enum cb_value_type typ);

#endif