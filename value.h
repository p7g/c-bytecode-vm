#ifndef cb_value_h
#define cb_value_h

#include <stdint.h>

#include "function.h"
#include "gc.h"
#include "string.h"

enum cb_value_type {
	CB_VALUE_INT,
	CB_VALUE_DOUBLE,
	CB_VALUE_BOOL,
	CB_VALUE_NULL,
	CB_VALUE_CHAR,
	CB_VALUE_STRING,
	CB_VALUE_ARRAY,
	CB_VALUE_FUNCTION,
};

struct cb_value;

typedef struct cb_array {
	size_t a_len;
	struct cb_value *a_values;
} cb_array;

typedef struct cb_value {
	cb_gc_header gc_header;
	enum cb_value_type v_type;
	union {
		intptr_t as_int;
		double as_double;
		int as_bool;
		uint32_t as_char;
		cb_str *as_string;
		cb_array *as_array;
		cb_function *as_function;
	} v_val;
} cb_value;

int cb_value_eq(cb_value *a, cb_value *b);
int cb_value_cmp(cb_value *a, cb_value *b);
int cb_value_to_string(cb_value *val);

void cb_value_init(cb_value *val);
void cb_value_deinit(cb_value *val);
void cb_value_mark(cb_value *val);

#endif
