#ifndef cb_eval_h
#define cb_eval_h

#include <stddef.h>

#include "compiler.h"
#include "gc.h"
#include "hashmap.h"
#include "value.h"

struct cb_upvalue {
	size_t refcount;
	int is_open;
	union {
		size_t idx;
		struct cb_value value;
	} v;
};

struct cb_frame {
	struct cb_frame *parent;
	size_t bp;
	struct cb_module *module;
	int is_function;
};

struct {
	cb_bytecode *bytecode;

	struct cb_value *stack;
	size_t sp, stack_size;

	struct cb_frame *frame;

	struct cb_upvalue **upvalues;
	size_t upvalues_idx, upvalues_size;

	/* size of this array is based on number of modspecs in agent */
	struct cb_module *modules;
} cb_vm_state;

void cb_vm_init(cb_bytecode *bytecode);
void cb_vm_deinit(void);

int cb_eval(size_t pc, struct cb_frame *frame);
int cb_run(void);
struct cb_user_function *cb_caller(void);
int cb_vm_call_user_func(struct cb_value fn, struct cb_value *args,
		size_t args_len, struct cb_value *result);
struct cb_value cb_get_upvalue(struct cb_upvalue *uv);

#endif
