#ifndef cb_eval_h
#define cb_eval_h

#include <stddef.h>

#include "compiler.h"
#include "value.h"

int cb_eval(cb_bytecode *bytecode);
struct cb_user_function *cb_caller(void);
struct cb_value cb_get_upvalue(size_t idx);

#endif
