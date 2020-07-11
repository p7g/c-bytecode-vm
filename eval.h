#ifndef cb_eval_h
#define cb_eval_h

#include <stddef.h>

#include "compiler.h"
#include "hashmap.h"
#include "value.h"

struct cb_upvalue {
	int is_open;
	union {
		size_t idx;
		struct cb_value value;
	} v;
};

struct frame;

struct {
	cb_bytecode *bytecode;

	struct cb_value *stack;
	size_t sp, stack_size;

	struct frame *frame;

	struct cb_upvalue **upvalues;
	size_t upvalues_idx, upvalues_size;

	/* size of this array is based on number of modspecs in agent */
	struct cb_module *modules;

	cb_hashmap *globals;
} cb_vm_state;

void cb_vm_init(cb_bytecode *bytecode);
void cb_vm_deinit(void);

int cb_run(void);
struct cb_user_function *cb_caller(void);
struct cb_value cb_get_upvalue(struct cb_upvalue *uv);

#endif
