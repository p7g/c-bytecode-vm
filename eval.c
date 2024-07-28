#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "cbcvm.h"
#include "cb_util.h"
#include "compiler.h"
#include "disassemble.h"
#include "error.h"
#include "eval.h"
#include "hashmap.h"
#include "intrinsics.h"
#include "opcode.h"
#include "value.h"
#include "str.h"
#include "struct.h"

#define STACK_MAX 30000
/* FIXME: we assume this is enough to evaluate the top level (since there is no
 * function for us to get stack effect from...
 *
 * Once there are code objects, we should store the stack effect there so we
 * can ensure there's enough stack space for modules too.
 */
#define STACK_INIT_SIZE 1024

struct cb_vm_state cb_vm_state;

void cb_vm_init(cb_bytecode *bytecode)
{
	cb_vm_state.bytecode = bytecode;
	cb_vm_state.ic = calloc(cb_bytecode_len(bytecode),
			sizeof(union cb_inline_cache));

	cb_vm_state.stack = malloc(STACK_INIT_SIZE * sizeof(struct cb_value));
	cb_vm_state.stack_size = STACK_INIT_SIZE;
	cb_vm_state.stack_top = cb_vm_state.stack;

	cb_vm_state.upvalues = calloc(32, sizeof(struct cb_upvalue *));
	cb_vm_state.upvalues_idx = 0;
	cb_vm_state.upvalues_size = 32;

	cb_vm_state.modules = calloc(cb_agent_modspec_count(),
			sizeof(struct cb_module));
}

void cb_vm_deinit(void)
{
	size_t num_modules = cb_agent_modspec_count();

	for (unsigned i = 0; i < num_modules; i += 1) {
		if (!cb_module_is_zero(cb_vm_state.modules[i]))
			cb_module_free(&cb_vm_state.modules[i]);
	}
	free(cb_vm_state.modules);
	cb_vm_state.modules = NULL;

	free(cb_vm_state.upvalues);
	cb_vm_state.upvalues = NULL;
	free(cb_vm_state.stack);
	free(cb_vm_state.ic);
	cb_vm_state.stack_top = NULL;
	cb_gc_collect();
}

void cb_vm_grow_modules_array(size_t new_size)
{
	struct cb_module *old_modules_ptr;

	cb_vm_state.modules = realloc((old_modules_ptr = cb_vm_state.modules),
			new_size * sizeof(struct cb_module));
	cb_module_zero(&cb_vm_state.modules[new_size - 1]);
	/* Patch all existing frames to point at the new modules */
	if (cb_vm_state.modules != old_modules_ptr) {
		for (struct cb_frame *current = cb_vm_state.frame;
				current; current = current->parent) {
			current->module = cb_vm_state.modules
					+ (current->module - old_modules_ptr);
		}
	}
}

static void add_upvalue(struct cb_upvalue *uv)
{
	if (cb_vm_state.upvalues_idx >= cb_vm_state.upvalues_size) {
		cb_vm_state.upvalues_size <<= 1;
		cb_vm_state.upvalues = realloc(cb_vm_state.upvalues,
				cb_vm_state.upvalues_size
				* sizeof(struct cb_upvalue *));
	}
	cb_vm_state.upvalues[cb_vm_state.upvalues_idx++] = uv;
}

inline struct cb_user_function *cb_caller(void)
{
	assert(CB_VALUE_IS_USER_FN(&cb_vm_state.stack[cb_vm_state.frame->bp]));
	return &cb_vm_state.stack[cb_vm_state.frame->bp]
		.val.as_function->value.as_user;
}

inline struct cb_value cb_get_upvalue(struct cb_upvalue *uv)
{
	if (uv->is_open)
		return cb_vm_state.stack[uv->v.idx];
	return uv->v.value;
}

int cb_eval(struct cb_vm_state *state, size_t pc, struct cb_frame *frame);

int cb_run(void)
{
	struct cb_frame frame;

	frame.parent = NULL;
	frame.module = NULL;
	frame.bp = 0;
	frame.func.type = CB_VALUE_NULL;

	return cb_eval(&cb_vm_state, 0, &frame);
}

static CB_INLINE void stack_push(struct cb_value v)
{
	ptrdiff_t stack_used = cb_vm_state.stack_top - cb_vm_state.stack;
	assert(stack_used >= 0);
	if ((size_t) stack_used >= cb_vm_state.stack_size) {
		cb_vm_state.stack_size <<= 2;
		cb_vm_state.stack = realloc(cb_vm_state.stack,
			cb_vm_state.stack_size * sizeof(struct cb_value));
		cb_vm_state.stack_top = cb_vm_state.stack + stack_used;
	}
	*cb_vm_state.stack_top++ = v;
}

static CB_INLINE struct cb_value stack_pop()
{
	assert(cb_vm_state.stack_top > cb_vm_state.stack);
	return *--cb_vm_state.stack_top;
}

int cb_vm_call_user_func(struct cb_value fn, struct cb_value *args,
		size_t args_len, struct cb_value *result)
{
	int ret;
	struct cb_frame frame;
	struct cb_user_function *func;

	assert(CB_VALUE_IS_USER_FN(&fn));
	func = &fn.val.as_function->value.as_user;

	frame.parent = cb_vm_state.frame;
	frame.func = fn;
	frame.bp = cb_vm_state.stack_top - cb_vm_state.stack;
	stack_push(fn);

	for (unsigned i = 0; i < args_len; i += 1)
		stack_push(args[i]);

	if (func->module_id != (size_t) -1)
		frame.module = &cb_vm_state.modules[func->module_id];
	else
		frame.module = NULL;

	ret = cb_eval(&cb_vm_state, func->address, &frame);
	*result = stack_pop();
	return ret;
}

/*
static void debug_state(cb_bytecode *bytecode, size_t pc, struct cb_frame *frame)
{
	size_t _name;
	cb_str modname_str, funcname_str;
	char *modname = "script";
	char *funcname;

	if (frame->module) {
		modname_str = cb_agent_get_string(cb_modspec_name(
					frame->module->spec));
		modname = cb_strptr(&modname_str);
	}

	if (!CB_VALUE_IS_USER_FN(&frame->func)) {
		funcname = "top";
	} else {
		_name = cb_vm_state.stack[frame->bp].val.as_function->name;
		funcname_str = cb_agent_get_string(_name);
		funcname = cb_strptr(&funcname_str);
	}

	printf("%s%s%s\n", modname, !CB_VALUE_IS_USER_FN(&frame->func)
			? " " : ".", funcname);
	printf("pc: %zu, bp: %zu, sp: %ld\n", pc, frame->bp,
			cb_vm_state.stack_top - cb_vm_state.stack);
	cb_disassemble_one(bytecode, pc);
	ptrdiff_t stack_used = cb_vm_state.stack_top - cb_vm_state.stack;
	int big_stack = stack_used > 10;
	printf("> %s", stack_used ? big_stack ? "... " : "" : "(empty)");
	int _first = 1;
	for (int _i = big_stack ? 10 : stack_used; _i > 0; _i -= 1) {
		struct cb_value *val = cb_vm_state.stack_top - _i;
		if (_first)
			_first = 0;
		else
			printf(", ");
		cb_str _str = cb_value_to_string(val);
		printf("%s", cb_strptr(&_str));
		cb_str_free(_str);
	}
	printf("\n\n");
}
*/

static void check_stack(struct cb_vm_state *state, struct cb_frame *frame)
{
	if (CB_VALUE_IS_USER_FN(&frame->func)) {
		struct cb_user_function *f =
			&frame->func.val.as_function->value.as_user;
		size_t stack_required = f->stack_required + f->nlocals;
		if (state->stack_top + stack_required
				> state->stack + state->stack_size) {
			state->stack_size <<= 2;
			state->stack = realloc(state->stack,
				state->stack_size * sizeof(struct cb_value));
		}
		// CB_VALUE_NULL is 0, so we can just set the whole thing to 0
		memset(state->stack_top, 0,
				f->nlocals * sizeof(struct cb_value));
		state->stack_top += f->nlocals;
	}
}

#define PUSH(V) (*stack_pointer++ = (V))
#define POP() (*--stack_pointer)

int cb_eval(struct cb_vm_state *state, size_t pc, struct cb_frame *frame)
{
	int retval = 0;
	cb_instruction *code = state->bytecode->code;
	struct cb_value *stack_pointer;
	state->frame = frame;
	size_t bp = frame->bp;

	check_stack(state, frame);
	stack_pointer = state->stack_top;

#define TABLE_ENTRY(OP) &&DO_##OP,
	static const void *dispatch_table[] = {
		CB_OPCODE_LIST(TABLE_ENTRY)
	};
#undef TABLE_ENTRY

#define NEXT() (code[pc++])
#define DISPATCH() ({ \
		/*if (cb_options.debug_vm) { \
			state->stack_top = stack_pointer; \
			debug_state(state->bytecode, pc, frame); \
		}*/ \
		size_t _next = NEXT(); \
		assert(_next < OP_MAX); \
		goto *dispatch_table[_next]; \
	})

#define RET_WITH_TRACE() ({ \
		cb_traceback_add_frame(frame); \
		goto end; \
	})
#define ERROR(MSG, ...) ({ \
		cb_error_set(cb_value_from_fmt((MSG), ##__VA_ARGS__)); \
		retval = 1; \
		RET_WITH_TRACE(); \
	})
#define READ_SIZE_T() (NEXT())
#define TOP() (stack_pointer[-1])
#define LOCAL_IDX(N) (bp + 1 + (N))
#define LOCAL(N) (state->stack[LOCAL_IDX(N)])
#define REPLACE(N, VAL) (state->stack[(N)] = (VAL))
#define GLOBALS() (frame->module->global_scope)
#define CACHE() (&state->ic[pc - 1])

	DISPATCH();

DO_OP_MAX:
DO_OP_HALT:
end:
	state->frame = state->frame->parent;
	if (retval && !state->frame) {
		fprintf(stderr, "Traceback (most recent call last):\n");
		struct cb_traceback *tb;
		for (tb = cb_error_tb(); tb; tb = tb->next)
			cb_traceback_print(stderr, tb);
		cb_str as_str = cb_value_to_string(cb_error_value());
		fprintf(stderr, "Uncaught error: %s\n", cb_strptr(&as_str));
		cb_str_free(as_str);
		cb_error_recover();
	}
	return retval;

DO_OP_CONST_INT: {
	size_t as_size_t = READ_SIZE_T();
	struct cb_value val;
	val.type = CB_VALUE_INT;
	val.val.as_int = (intptr_t) as_size_t;
	PUSH(val);
	DISPATCH();

DO_OP_CONST_DOUBLE: {
	struct cb_value val;
	union {
		double as_double;
		size_t as_size_t;
	} doubleval;
	doubleval.as_size_t = READ_SIZE_T();
	val.type = CB_VALUE_DOUBLE;
	val.val.as_double = doubleval.as_double;
	PUSH(val);
	DISPATCH();
}

DO_OP_CONST_STRING: {
	struct cb_value val;
	val.type = CB_VALUE_INTERNED_STRING;
	val.val.as_interned_string = READ_SIZE_T();
	PUSH(val);
	DISPATCH();
}

DO_OP_CONST_CHAR: {
	struct cb_value val;
	val.type = CB_VALUE_CHAR;
	val.val.as_char = (uint32_t) READ_SIZE_T();
	PUSH(val);
	DISPATCH();
}

DO_OP_CONST_TRUE: {
	struct cb_value val;
	val.type = CB_VALUE_BOOL;
	val.val.as_bool = 1;
	PUSH(val);
	DISPATCH();
}

DO_OP_CONST_FALSE: {
	struct cb_value val;
	val.type = CB_VALUE_BOOL;
	val.val.as_bool = 0;
	PUSH(val);
	DISPATCH();
}

DO_OP_CONST_NULL: {
	struct cb_value val;
	val.type = CB_VALUE_NULL;
	PUSH(val);
	DISPATCH();
}

#define NUMBER_BINOP(OP) ({ \
		struct cb_value a, b, result; \
		b = POP(); \
		a = POP(); \
		if (a.type == CB_VALUE_INT && b.type == CB_VALUE_INT) { \
			result.type = CB_VALUE_INT; \
			result.val.as_int = a.val.as_int OP b.val.as_int; \
		} else if (a.type == CB_VALUE_DOUBLE \
				&& b.type == CB_VALUE_DOUBLE) { \
			result.type = CB_VALUE_DOUBLE; \
			result.val.as_double = a.val.as_double \
					OP b.val.as_double; \
		} else if (a.type == CB_VALUE_INT \
				&& b.type == CB_VALUE_DOUBLE) { \
			result.type = CB_VALUE_DOUBLE; \
			result.val.as_double = a.val.as_int \
					OP b.val.as_double; \
		} else if (a.type == CB_VALUE_DOUBLE \
				&& b.type == CB_VALUE_INT) { \
			result.type = CB_VALUE_DOUBLE; \
			result.val.as_double = a.val.as_double \
					OP b.val.as_int; \
		} else { \
			ERROR("Invalid operands for %s\n", #OP); \
		} \
		PUSH(result); \
	})

DO_OP_ADD:
	NUMBER_BINOP(+);
	DISPATCH();

DO_OP_SUB:
	NUMBER_BINOP(-);
	DISPATCH();

DO_OP_MUL:
	NUMBER_BINOP(*);
	DISPATCH();

DO_OP_DIV:
	NUMBER_BINOP(/);
	DISPATCH();

DO_OP_MOD: {
	struct cb_value a, b, result;
	b = POP();
	a = POP();
	if (a.type != CB_VALUE_INT || b.type != CB_VALUE_INT)
		ERROR("Operands to '%%' must be integers\n");
	result.type = CB_VALUE_INT;
	result.val.as_int = a.val.as_int % b.val.as_int;
	PUSH(result);
	DISPATCH();
}

DO_OP_EXP: {
	struct cb_value a, b, result;
	b = POP();
	a = POP();
	result.type = CB_VALUE_DOUBLE;
	result.val.as_double = pow(
			a.type == CB_VALUE_INT
			? (double) a.val.as_int
			: a.val.as_double,
			b.type == CB_VALUE_INT
			? (double) b.val.as_int
			: b.val.as_double);
	PUSH(result);
	DISPATCH();
}

DO_OP_JUMP: {
	size_t dest = READ_SIZE_T();
	pc = dest;
	DISPATCH();
}

DO_OP_JUMP_IF_TRUE: {
	struct cb_value pred;
	size_t next = READ_SIZE_T();
	pred = POP();
	if (pred.type == CB_VALUE_BOOL) {
		if (pred.val.as_bool)
			pc = next;
	} else if (cb_value_is_truthy(&pred)) {
		pc = next;
	}
	DISPATCH();
}

DO_OP_JUMP_IF_FALSE: {
	struct cb_value pred;
	size_t next = READ_SIZE_T();
	pred = POP();
	if (pred.type == CB_VALUE_BOOL) {
		if (!pred.val.as_bool)
			pc = next;
	} else if (!cb_value_is_truthy(&pred)) {
		pc = next;
	}
	DISPATCH();
}

DO_OP_CALL: {
	size_t num_args, name;
	struct cb_value func_val, result;
	struct cb_function *func;
	struct cb_frame next_frame;
	int failed;

	num_args = READ_SIZE_T();
	func_val = stack_pointer[-num_args - 1];

	if (func_val.type != CB_VALUE_FUNCTION)
		ERROR("Value of type '%s' is not callable\n",
				cb_value_type_friendly_name(func_val.type));

	func = func_val.val.as_function;
	name = func->name;
	if (func->arity > num_args) {
		cb_str s = cb_agent_get_string(name);
		ERROR("Too few arguments to function '%s'\n", cb_strptr(&s));
	}
	state->stack_top = stack_pointer;
	if (func->type == CB_FUNCTION_NATIVE) {
		failed = func->value.as_native(num_args,
				&stack_pointer[-num_args], &result);
		/* native functions can mess with global state */
		stack_pointer = state->stack_top;
		assert((size_t) (stack_pointer - state->stack) > num_args);
		stack_pointer -= (num_args + 1);
		PUSH(result);
		if (failed) {
			retval = 1;
			RET_WITH_TRACE();
		}
	} else {
		ptrdiff_t stack_used = state->stack_top - state->stack;
		next_frame.parent = frame;
		next_frame.func = func_val;
		next_frame.bp = stack_used - num_args - 1;
		if (func->value.as_user.module_id != (size_t) -1)
			next_frame.module = &state->modules[
				func->value.as_user.module_id];
		else
			next_frame.module = NULL;
		if (cb_eval(state, cb_ufunc_entry(func, num_args),
					&next_frame)) {
			retval = 1;
			RET_WITH_TRACE();
		}
		stack_pointer = state->stack_top;
	}
	code = state->bytecode->code;

	DISPATCH();
}

DO_OP_RETURN: {
	int i;
	struct cb_value retval;
	struct cb_upvalue *uv;

	retval = POP();

	/* close upvalues */
	if (state->upvalues_idx != 0) {
		for (i = state->upvalues_idx - 1; i >= 0; i -= 1) {
			uv = state->upvalues[i];
			if (!uv || !uv->is_open || uv->v.idx < frame->bp)
				break;
			uv->is_open = 0;
			uv->v.value = state->stack[uv->v.idx];
		}
		state->upvalues_idx = i + 1;
	}

	stack_pointer = state->stack + frame->bp;
	PUSH(retval);
	state->stack_top = stack_pointer;
	goto end;
}

DO_OP_POP:
	(void) POP();
	DISPATCH();

DO_OP_LOAD_LOCAL: {
	size_t local_no;
	struct cb_value value;

	local_no = READ_SIZE_T();
	value = LOCAL(local_no);
	PUSH(value);

	DISPATCH();
}

DO_OP_STORE_LOCAL: {
	size_t local_no;
	size_t local_idx;
	struct cb_value value;

	local_no = READ_SIZE_T();
	local_idx = LOCAL_IDX(local_no);
	value = TOP();
	REPLACE(local_idx, value);

	DISPATCH();
}

DO_OP_LOAD_GLOBAL: {
	size_t id;
	struct cb_value value;

	id = READ_SIZE_T();

	struct cb_load_global_cache *ic = &CACHE()->load_global;
	/* non-zero version means that there is a cache. */
	if (ic->version != 0 && ic->version == cb_hashmap_version(GLOBALS())) {
		value = cb_hashmap_get_index(GLOBALS(), ic->index);
	} else {
		int empty;
		ssize_t idx = cb_hashmap_find(GLOBALS(), id, &empty);
		if (empty) {
			cb_str s = cb_agent_get_string(id);
			ERROR("Unbound global '%s'", cb_strptr(&s));
		}
		value = cb_hashmap_get_index(GLOBALS(), idx);
	}

	PUSH(value);
	DISPATCH();
}

DO_OP_DECLARE_GLOBAL: {
	size_t id;
	struct cb_value null_value;

	id = READ_SIZE_T();
	null_value.type = CB_VALUE_NULL;
	cb_hashmap_set(GLOBALS(), id, null_value);
	DISPATCH();
}

DO_OP_STORE_GLOBAL: {
	size_t id;

	id = READ_SIZE_T();

	struct cb_load_global_cache *ic = &CACHE()->load_global;
	if (ic->version != 0 && ic->version == cb_hashmap_version(GLOBALS())) {
		cb_hashmap_set_index(GLOBALS(), ic->index, TOP());
	} else {
		int empty;
		ssize_t idx = cb_hashmap_find(GLOBALS(), id, &empty);
		/* ignore storing globals that haven't been declared */
		if (!empty) {
			ic->index = idx;
			ic->version = cb_hashmap_version(GLOBALS());
			cb_hashmap_set_index(GLOBALS(), idx, TOP());
		}
	}

	DISPATCH();
}

DO_OP_NEW_FUNCTION: {
	size_t name, arity, address, nopt, i, stack_required, nlocals;
	struct cb_function *func;
	struct cb_value func_val;

	name = READ_SIZE_T();
	arity = READ_SIZE_T();
	address = READ_SIZE_T();
	nlocals = READ_SIZE_T();
	stack_required = READ_SIZE_T();
	nopt = READ_SIZE_T();

	func = cb_function_new();
	func->type = CB_FUNCTION_USER;
	func->name = name;
	func->arity = arity - nopt;
	func->value.as_user = (struct cb_user_function) {
		.address = address,
		.upvalues = NULL,
		.upvalues_len = 0,
		.upvalues_size = 0,
		.module_id = frame->module
			? cb_modspec_id(frame->module->spec)
			: (size_t) -1,
		.nlocals = nlocals,
		.stack_required = stack_required,
		.optargs = (struct cb_function_optargs) {
			.count = nopt,
		},
	};

	for (i = 0; i < nopt; i += 1)
		func->value.as_user.optargs.addrs[i] = READ_SIZE_T();

	func_val.type = CB_VALUE_FUNCTION;
	func_val.val.as_function = func;

	PUSH(func_val);
	DISPATCH();
}

DO_OP_BIND_LOCAL: {
	int i;
	struct cb_value *func;
	struct cb_upvalue *uv;
	size_t idx = LOCAL_IDX(READ_SIZE_T());

	func = &TOP();
	if (func->type != CB_VALUE_FUNCTION
			|| func->val.as_function->type != CB_FUNCTION_USER)
		ERROR("Can only bind upvalues to user functions");

	uv = NULL;
	for (i = state->upvalues_idx - 1; i >= 0; i -= 1) {
		if (state->upvalues[i] == NULL
				|| !state->upvalues[i]->is_open
				|| state->upvalues[i]->v.idx < frame->bp)
			break;
		if (state->upvalues[i]->v.idx == idx) {
			uv = state->upvalues[i];
			break;
		}
	}

	if (!uv) {
		uv = malloc(sizeof(struct cb_upvalue));
		uv->refcount = 0;
		uv->is_open = 1;
		uv->v.idx = idx;

		add_upvalue(uv);
	}

	cb_function_add_upvalue(&func->val.as_function->value.as_user, uv);
	DISPATCH();
}

DO_OP_BIND_UPVALUE: {
	struct cb_value *self, *func;
	size_t upvalue_idx = READ_SIZE_T();

	self = &state->stack[frame->bp];
	assert(CB_VALUE_IS_USER_FN(self));
	func = &TOP();
	if (!CB_VALUE_IS_USER_FN(func))
		ERROR("Can only bind upvalues to user functions");

	cb_function_add_upvalue(&func->val.as_function->value.as_user,
		self->val.as_function->value.as_user.upvalues[upvalue_idx]);

	DISPATCH();
}

DO_OP_LOAD_UPVALUE: {
	struct cb_value *self;
	struct cb_upvalue *uv;
	size_t idx = READ_SIZE_T();

	self = &state->stack[frame->bp];
	assert(CB_VALUE_IS_USER_FN(self));

	uv = self->val.as_function->value.as_user.upvalues[idx];
	if (uv->is_open)
		PUSH(state->stack[uv->v.idx]);
	else
		PUSH(uv->v.value);

	DISPATCH();
}

DO_OP_STORE_UPVALUE: {
	struct cb_value *self;
	struct cb_upvalue *uv;
	size_t idx = READ_SIZE_T();

	self = &state->stack[frame->bp];
	assert(CB_VALUE_IS_USER_FN(self));

	uv = self->val.as_function->value.as_user.upvalues[idx];
	if (uv->is_open)
		state->stack[uv->v.idx] = TOP();
	else
		uv->v.value = TOP();

	DISPATCH();
}

DO_OP_LOAD_FROM_MODULE: {
	size_t mod_id, export_id, export_name;
	struct cb_module *mod;
	struct cb_value val;
	size_t idx;
	
	mod_id = READ_SIZE_T();
	export_id = READ_SIZE_T();
	mod = &state->modules[mod_id];

	struct cb_load_from_module_cache *ic = &CACHE()->load_from_module;

	if (ic->version == 0) {
		int empty;
		export_name = cb_modspec_get_export_name(mod->spec, export_id);
		idx = cb_hashmap_find(mod->global_scope, export_name, &empty);
		/* Exports are verified at compile time */
		assert(!empty);
		ic->version = cb_hashmap_version(mod->global_scope);
		ic->index = idx;
	} else {
		assert(ic->version == cb_hashmap_version(mod->global_scope));
	}

	val = cb_hashmap_get_index(mod->global_scope, ic->index);

	PUSH(val);
	DISPATCH();
}

DO_OP_NEW_ARRAY_WITH_VALUES: {
	struct cb_value arrayval;
	size_t size = READ_SIZE_T();
	struct cb_array *array = cb_array_new(size);
	array->len = size;

	memcpy(array->values, stack_pointer - size,
			size * sizeof(struct cb_value));
	stack_pointer -= size;

	arrayval.type = CB_VALUE_ARRAY;
	arrayval.val.as_array = array;
	PUSH(arrayval);
	DISPATCH();
}

#define EXPECT_INT_INDEX(V) ({ \
		struct cb_value v = (V); \
		if (v.type != CB_VALUE_INT) \
			ERROR("Array index must be integer, got %s", \
					cb_value_type_friendly_name(v.type)); \
		v.val.as_int; \
	})
#define ARRAY_PTR(ARR, IDX) ({ \
		struct cb_value _arr = (ARR); \
		if (_arr.type != CB_VALUE_ARRAY) \
			ERROR("Can only index arrays, got %s", \
					cb_value_type_friendly_name(_arr.type)); \
		size_t _idx = EXPECT_INT_INDEX(IDX); \
		if (_idx >= _arr.val.as_array->len) \
			ERROR("Index %zu greater than array length %zu", \
					_idx, _arr.val.as_array->len); \
		&_arr.val.as_array->values[_idx]; \
	})

DO_OP_ARRAY_GET: {
	struct cb_value arr, idx, val;
	idx = POP();
	arr = POP();
	val = *ARRAY_PTR(arr, idx);
	PUSH(val);
	DISPATCH();
}

DO_OP_ARRAY_SET: {
	struct cb_value arr, idx, val;
	val = POP();
	idx = POP();
	arr = POP();
	*ARRAY_PTR(arr, idx) = val;
	PUSH(val);
	DISPATCH();
}

#undef EXPECT_INT_INDEX
#undef ARRAY_PTR

DO_OP_EQUAL: {
	struct cb_value result, a, b;
	b = POP();
	a = POP();
	result.type = CB_VALUE_BOOL;
	result.val.as_bool = cb_value_eq(&a, &b);
	PUSH(result);
	DISPATCH();
}

DO_OP_NOT_EQUAL: {
	struct cb_value result, a, b;
	b = POP();
	a = POP();
	result.type = CB_VALUE_BOOL;
	result.val.as_bool = !cb_value_eq(&a, &b);
	PUSH(result);
	DISPATCH();
}

#define CMP(A, B) ({ \
		int _ok; \
		double _result = cb_value_cmp(&(A), &(B), &_ok); \
		if (!_ok) \
			ERROR("Cannot compare values of types %s and %s", \
					cb_value_type_friendly_name((A).type), \
					cb_value_type_friendly_name((B).type)); \
		_result; \
	})

DO_OP_LESS_THAN: {
	double diff;
	struct cb_value result, a, b;
	b = POP();
	a = POP();
	result.type = CB_VALUE_BOOL;
	diff = CMP(a, b);
	result.val.as_bool = diff < 0;
	PUSH(result);
	DISPATCH();
}

DO_OP_LESS_THAN_EQUAL: {
	double diff;
	struct cb_value result, a, b;
	b = POP();
	a = POP();
	result.type = CB_VALUE_BOOL;
	diff = CMP(a, b);
	result.val.as_bool = diff <= 0;
	PUSH(result);
	DISPATCH();
}

DO_OP_GREATER_THAN: {
	double diff;
	struct cb_value result, a, b;
	b = POP();
	a = POP();
	result.type = CB_VALUE_BOOL;
	diff = CMP(a, b);
	result.val.as_bool = diff > 0;
	PUSH(result);
	DISPATCH();
}

DO_OP_GREATER_THAN_EQUAL: {
	double diff;
	struct cb_value result, a, b;
	b = POP();
	a = POP();
	result.type = CB_VALUE_BOOL;
	diff = CMP(a, b);
	result.val.as_bool = diff >= 0;
	PUSH(result);
	DISPATCH();
}

#undef CMP

#define BITOP(OP) ({ \
		struct cb_value a, b, result; \
		b = POP(); \
		a = POP(); \
		if (a.type != CB_VALUE_INT) \
			ERROR("Bitwise operands must be integers, got %s", \
					cb_value_type_friendly_name(a.type)); \
		if (b.type != CB_VALUE_INT) \
			ERROR("Bitwise operands must be integers, got %s", \
					cb_value_type_friendly_name(b.type)); \
		result.type = CB_VALUE_INT; \
		result.val.as_int = a.val.as_int OP b.val.as_int; \
		PUSH(result); \
	})

DO_OP_BITWISE_AND:
	BITOP(&);
	DISPATCH();

DO_OP_BITWISE_OR:
	BITOP(|);
	DISPATCH();

DO_OP_BITWISE_XOR:
	BITOP(^);
	DISPATCH();

DO_OP_BITWISE_NOT: {
	struct cb_value a;
	a = POP();
	if (a.type != CB_VALUE_INT)
		ERROR("Bitwise operands must be integers, got %s",
				cb_value_type_friendly_name(a.type));
	a.val.as_int = ~a.val.as_int;
	PUSH(a);
	DISPATCH();
}

#undef BITOP

DO_OP_NOT: {
	struct cb_value a, result;
	a = POP();
	result.type = CB_VALUE_BOOL;
	if (a.type == CB_VALUE_BOOL)
		result.val.as_bool = !a.val.as_bool;
	else
		result.val.as_bool = !cb_value_is_truthy(&a);
	PUSH(result);
	DISPATCH();
}

DO_OP_NEG: {
	struct cb_value a;
	a = POP();
	if (a.type == CB_VALUE_INT) {
		a.val.as_int = -a.val.as_int;
	} else if (a.type == CB_VALUE_DOUBLE) {
		a.val.as_double = -a.val.as_double;
	} else {
		ERROR("Operand for negation must be int or double, got %s",
				cb_value_type_friendly_name(a.type));
	}
	PUSH(a);
	DISPATCH();
}

DO_OP_INIT_MODULE: {
	size_t module_id;
	const cb_modspec *spec;
	struct cb_module mod;

	module_id = READ_SIZE_T();
	spec = cb_agent_get_modspec(module_id);
	assert(spec != NULL);
	mod.spec = spec;
	mod.global_scope = cb_hashmap_new();
	state->modules[module_id] = mod;
	frame->module = &state->modules[module_id];
	make_intrinsics(mod.global_scope);

	DISPATCH();
}

DO_OP_ENTER_MODULE:
	frame->module = &state->modules[READ_SIZE_T()];
	DISPATCH();


DO_OP_END_MODULE:
DO_OP_EXIT_MODULE:
	frame->module = NULL;
	DISPATCH();

DO_OP_DUP: {
	struct cb_value val = TOP();
	PUSH(val);
	DISPATCH();
}

DO_OP_LOAD_STRUCT: {
	size_t fname = READ_SIZE_T();
	struct cb_value recv = POP();
	if (recv.type != CB_VALUE_STRUCT)
		ERROR("Cannot get field of non-struct type %s",
				cb_value_type_friendly_name(recv.type));
	struct cb_struct *s = recv.val.as_struct;

	struct cb_load_struct_cache *ic = &CACHE()->load_struct;

	struct cb_value *val;
	if (ic->spec != NULL) {
		if (ic->spec == s->spec) {
			val = &s->fields[ic->index];
			PUSH(*val);
			DISPATCH();
		}
		/* deopt */
		ic->spec = NULL;
		ic->index = -1;
	}
	ssize_t idx;
	val = cb_struct_get_field(s, fname, &idx);
	if (!val) {
		cb_str fname_str, specname_str;

		fname_str = cb_agent_get_string(fname);
		specname_str = cb_agent_get_string(s->spec->name);
		ERROR("No such field '%s' on struct '%s'",
				cb_strptr(&fname_str),
				cb_strptr(&specname_str));
	}
	if (ic->index != -1) {
		ic->spec = s->spec;
		ic->index = idx;
	}
	PUSH(*val);
	DISPATCH();
}

#define DO_STORE_STRUCT_FIELD(RET) ({ \
	size_t fname = READ_SIZE_T(); \
	struct cb_value val = POP(); \
	struct cb_value recv = POP(); \
	if (recv.type != CB_VALUE_STRUCT) \
		ERROR("Cannot set field of non-struct type %s", \
				cb_value_type_friendly_name(recv.type)); \
	struct cb_struct *s = recv.val.as_struct; \
	struct cb_load_struct_cache *ic = &CACHE()->load_struct; \
	if (ic->spec != NULL) { \
		if (ic->spec == s->spec) { \
			s->fields[ic->index] = val; \
			PUSH(RET); \
			DISPATCH(); \
		} \
		/* deopt */ \
		ic->spec = NULL; \
		ic->index = -1; \
	} \
	ssize_t idx; \
	if (cb_struct_set_field(s, fname, val, &idx)) { \
		cb_str _fname_str, _specname_str; \
		_fname_str = cb_agent_get_string(fname); \
		_specname_str = cb_agent_get_string(s->spec->name); \
		ERROR("No such field '%s' on struct '%s'", \
				cb_strptr(&_fname_str), \
				cb_strptr(&_specname_str)); \
	} \
	if (ic->index != -1) { \
		ic->spec = s->spec; \
		ic->index = idx; \
	} \
	RET; \
	})

/* These 2 differ in whether they leave the struct or the value on the stack */
DO_OP_ADD_STRUCT_FIELD: {
	struct cb_value result = DO_STORE_STRUCT_FIELD(recv);
	PUSH(result);
	DISPATCH();
}

DO_OP_STORE_STRUCT: {
	struct cb_value result = DO_STORE_STRUCT_FIELD(val);
	PUSH(result);
	DISPATCH();
}

#undef DO_STORE_STRUCT_FIELD

DO_OP_NEW_STRUCT: {
	struct cb_value struct_obj;
	struct cb_value spec_obj = POP();
	if (spec_obj.type != CB_VALUE_STRUCT_SPEC) {
		ERROR("Cannot instantiate struct from %s object",
				cb_value_type_friendly_name(spec_obj.type));
	}
	struct_obj.type = CB_VALUE_STRUCT;
	struct_obj.val.as_struct = cb_struct_spec_instantiate(
			spec_obj.val.as_struct_spec);
	PUSH(struct_obj);
	DISPATCH();
}

DO_OP_NEW_STRUCT_SPEC: {
	size_t name_id, nfields;
	struct cb_value val;
	name_id = READ_SIZE_T();
	nfields = READ_SIZE_T();
	val.type = CB_VALUE_STRUCT_SPEC;
	val.val.as_struct_spec = cb_struct_spec_new(name_id, nfields);
	for (size_t i = 0; i < nfields; i += 1) {
		size_t field_id = READ_SIZE_T();
		cb_struct_spec_set_field_name(val.val.as_struct_spec, i,
				field_id);
	}
	PUSH(val);
	DISPATCH();
}

DO_OP_ROT_2: {
	struct cb_value a, b;
	a = POP();
	b = POP();
	PUSH(a);
	PUSH(b);
	DISPATCH();
}
}

#undef CACHE
#undef DISPATCH
#undef ERROR
#undef GLOBALS
#undef LOCAL
#undef LOCAL_IDX
#undef NEXT
#undef READ_SIZE_T
#undef REPLACE
#undef TOP
}

#undef PUSH
#undef POP