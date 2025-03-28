#ifndef cb_eval_h
#define cb_eval_h

#include <stddef.h>

#include "code.h"
#include "compiler.h"
#include "gc.h"
#include "hashmap.h"
#include "value.h"

struct cb_upvalue {
	size_t refcount;
	int is_closed;
	union {
		size_t idx;
		struct cb_value value;
	} v;
};

struct cb_frame {
	struct cb_frame *parent;
	size_t module_id;
	int is_function, is_native;
	unsigned num_args;
	struct cb_code *code;
	size_t bp;
	/* A pointer to the sp variable for the GC */
	struct cb_value *const *sp;
};

union cb_inline_cache {
	struct cb_load_struct_cache {
		const struct cb_struct_spec *spec;
		ssize_t index;
	} load_struct;
	struct cb_load_global_cache {
		size_t version;
		size_t index;
	} load_global;
	struct cb_load_from_module_cache {
		size_t version;
		size_t index;
	} load_from_module;
};


struct cb_vm_state {
	struct cb_frame *frame;
	struct cb_value *stack;
	size_t stack_size;

	struct cb_upvalue **upvalues;
	size_t upvalues_idx, upvalues_size;

	/* size of this array is based on number of modspecs in agent */
	struct cb_module *modules;

	struct cb_error *error;
};

extern struct cb_vm_state cb_vm_state;

void cb_vm_init(void);
void cb_vm_deinit(void);
void cb_vm_grow_modules_array();

int cb_run(struct cb_code *code);
int cb_vm_call(struct cb_value fn, struct cb_value *args, size_t args_len,
		struct cb_value *result);
struct cb_value cb_load_upvalue(struct cb_upvalue *uv);
void cb_store_upvalue(struct cb_upvalue *uv, struct cb_value val);

#endif