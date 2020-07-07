#ifndef cb_eval_h
#define cb_eval_h

#include <stddef.h>

#include "compiler.h"
#include "value.h"

struct cb_upvalue {
	int is_open;
	union {
		size_t idx;
		struct cb_value value;
	} v;
};

int cb_eval(cb_bytecode *bytecode);
struct cb_user_function *cb_caller(void);
struct cb_value cb_get_upvalue(struct cb_upvalue *uv);

#endif
