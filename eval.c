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

struct cb_vm_state cb_vm_state = {0};

void init_module(struct cb_module *mod, const cb_modspec *spec)
{
	mod->spec = spec;
	mod->global_scope = cb_hashmap_new();
	mod->evaluated = 0;
	make_intrinsics(mod->global_scope);
}

void cb_vm_init(void)
{
	cb_vm_state.upvalues = calloc(32, sizeof(struct cb_upvalue *));
	cb_vm_state.upvalues_idx = 0;
	cb_vm_state.upvalues_size = 32;
	cb_vm_state.call_depth = 0;

	cb_vm_state.modules = calloc(cb_agent_modspec_count(),
			sizeof(struct cb_module));
	for (size_t i = 0; i < cb_agent_modspec_count(); i += 1)
		init_module(&cb_vm_state.modules[i], cb_agent_get_modspec(i));
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
	cb_gc_collect();
}

/* FIXME: Either always grow by 1 or zero out all new modules */
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

static struct cb_frame *find_upvalue_frame(struct cb_upvalue *uv)
{
	assert(uv->call_depth >= 0);
	assert((size_t) uv->call_depth <= cb_vm_state.call_depth);

	struct cb_frame *frame = cb_vm_state.frame;
	for (int i = cb_vm_state.call_depth; i > uv->call_depth; i -= 1)
		frame = frame->parent;

	return frame;
}

inline struct cb_value cb_load_upvalue(struct cb_upvalue *uv)
{
	if (uv->call_depth < 0)
		return uv->v.value;

	struct cb_frame *frame = find_upvalue_frame(uv);
	assert(frame);
	return frame->stack[uv->v.idx];
}

static CB_INLINE void store_upvalue(struct cb_upvalue *uv, struct cb_value val)
{
	if (uv->call_depth < 0) {
		uv->v.value = val;
		return;
	}

	struct cb_frame *frame = find_upvalue_frame(uv);
	assert(frame);

	frame->stack[uv->v.idx] = val;
}

int cb_eval(struct cb_frame *frame);

int cb_run(struct cb_code *code)
{
	struct cb_frame frame;
	int result;

	frame.module = &cb_vm_state.modules[cb_modspec_id(code->modspec)];
	if (cb_module_is_zero(*frame.module))
		init_module(frame.module, code->modspec);
	frame.is_function = 0;
	frame.num_args = 0;
	frame.code = code;
	frame.stack = alloca(sizeof(struct cb_value) * code->stack_size);
	frame.sp = NULL; /* pointer to local variable, assigned in cb_eval */

	result = cb_eval(&frame);
	frame.module->evaluated = 1;
	return result;
}

int cb_vm_call(struct cb_value fn, struct cb_value *args, size_t args_len,
		struct cb_value *result)
{
	int ret;
	size_t stack_size;
	struct cb_frame frame;
	struct cb_user_function *user_func;
	cb_native_function *native_func;

	if (fn.type != CB_VALUE_FUNCTION) {
		cb_error_set(cb_value_from_fmt("Value of type '%s' is not callable",
			cb_value_type_friendly_name(fn.type)));
		return -1;
	}

	if (fn.val.as_function->type == CB_FUNCTION_USER) {
		user_func = &fn.val.as_function->value.as_user;

		stack_size = user_func->code->stack_size + args_len;

		frame.is_function = 1;
		frame.num_args = args_len;
		frame.code = user_func->code;
		frame.module = &cb_vm_state.modules[
			cb_modspec_id(user_func->code->modspec)];
		if (cb_module_is_zero(*frame.module))
			init_module(frame.module, user_func->code->modspec);

		/* FIXME: add some restriction on arguments length */
		frame.stack = alloca(stack_size * sizeof(struct cb_value));
		frame.stack[0] = fn;

		memcpy(&frame.stack[1], args,
				args_len * sizeof(struct cb_value));

		ret = cb_eval(&frame);
		/* the result replaces the function on the stack */
		*result = frame.stack[0];
	} else {
		native_func = fn.val.as_function->value.as_native;
		ret = native_func(args_len, args, result);
	}
	return ret;
}

#ifdef CB_DEBUG_VM
static void debug_state(size_t sp, size_t pc, struct cb_frame *frame)
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

	if (!frame->is_function) {
		funcname = "top";
	} else {
		funcname_str = cb_agent_get_string(_name);
		funcname = cb_strptr(&funcname_str);
	}

	printf("%s%s%s\n", modname, !frame->is_function ? " " : ".", funcname);
	printf("pc: %zu, sp: %zu, stack_size: %hu\n", pc, sp, frame->code->stack_size);
	cb_disassemble_one(frame->code->bytecode + pc, pc);
	printf("> %s", sp ? (sp > 10 ? "... " : "") : "(empty)");
	int _first = 1;
	for (int _i = (10 < sp ? 10 : sp); _i > 0; _i -= 1) {
		int _idx = sp - _i;
		if (_first)
			_first = 0;
		else
			printf(", ");
		cb_str _str = cb_value_to_string(frame->stack[_idx]);
		printf("%s", cb_strptr(&_str));
		cb_str_free(_str);
	}
	printf("\n\n");
}
#endif

int cb_eval(struct cb_frame *frame)
{
#define TABLE_ENTRY(OP) &&DO_##OP,
	static const void *dispatch_table[] = {
		CB_OPCODE_LIST(TABLE_ENTRY)
	};
#undef TABLE_ENTRY

#ifdef CB_DEBUG_VM
# define DEBUG_STATE() \
	debug_state(sp - frame->stack, ip - frame->code->bytecode, frame)
#else
# define DEBUG_STATE()
#endif

#define NEXT() (*ip++)
#define DISPATCH() ({ \
		DEBUG_STATE(); \
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
#define TOP() (sp[-1])
#define LOCAL_IDX(N) ((N) + 1)
#define LOCAL(N) (locals[N])
#define REPLACE_LOCAL(N, VAL) ({ \
		struct cb_value _v = (VAL); \
		locals[N] = _v; \
	})
#define GLOBALS() (frame->module->global_scope)
#define PUSH(V) (*sp++ = (V))
#define POP() (*--sp)
#define CACHE() (&frame->code->ic[ip - frame->code->bytecode - 1])

	struct cb_value *sp = frame->stack + frame->is_function + frame->num_args;
	cb_instruction *ip = frame->code->bytecode;
	int retval = 0;
	struct cb_value *locals = frame->stack + frame->is_function;
	struct cb_const *consts = frame->code->const_pool;

	frame->sp = &sp;
	frame->parent = cb_vm_state.frame;
	cb_vm_state.frame = frame;
	cb_vm_state.call_depth += 1;
	DISPATCH();

DO_OP_MAX:
DO_OP_HALT:
end:
	cb_vm_state.frame = cb_vm_state.frame->parent;
	cb_vm_state.call_depth -= 1;
	if (retval && !cb_vm_state.frame) {
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

DO_OP_LOAD_CONST: {
	struct cb_const cst = consts[READ_SIZE_T()];
	struct cb_value as_value = cb_const_to_value(&cst);
	PUSH(as_value);
	DISPATCH();
}

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

#define NUMBER_BINOP(OP, INT_OP) ({ \
		struct cb_value a, b, result; \
		b = POP(); \
		a = POP(); \
		if (a.type == CB_VALUE_INT && b.type == CB_VALUE_INT) { \
			result.type = CB_VALUE_INT; \
			result.val.as_int = a.val.as_int OP b.val.as_int; \
			if (INT_OP(a.val.as_int, b.val.as_int, \
						&result.val.as_int)) { \
				ERROR("Integer overflow in %s\n", #OP); \
			} \
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
	NUMBER_BINOP(+, __builtin_add_overflow);
	DISPATCH();

DO_OP_SUB:
	NUMBER_BINOP(-, __builtin_sub_overflow);
	DISPATCH();

DO_OP_MUL:
	NUMBER_BINOP(*, __builtin_mul_overflow);
	DISPATCH();

DO_OP_DIV:
#define INT_DIV(a, b, result) ((*(result) = (a) / (b)), 0)
	NUMBER_BINOP(/, INT_DIV);
#undef INT_DIV
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
	/* XXX: JUMP macro */
	ip = frame->code->bytecode + dest;
	DISPATCH();
}

DO_OP_JUMP_IF_TRUE: {
	struct cb_value pred;
	size_t next = READ_SIZE_T();
	pred = POP();
	if ((pred.type == CB_VALUE_BOOL && pred.val.as_bool)
			|| cb_value_is_truthy(&pred))
		ip = frame->code->bytecode + next;
	DISPATCH();
}

DO_OP_JUMP_IF_FALSE: {
	struct cb_value pred;
	size_t next = READ_SIZE_T();
	pred = POP();
	if ((pred.type == CB_VALUE_BOOL && !pred.val.as_bool)
			|| !cb_value_is_truthy(&pred))
		ip = frame->code->bytecode + next;
	DISPATCH();
}

DO_OP_CALL: {
	size_t num_args, name;
	struct cb_value func_val, result;
	struct cb_function *func;
	int failed;
	int num_opt_params;

	num_args = READ_SIZE_T();
	func_val = *(sp - num_args - 1);

	if (func_val.type != CB_VALUE_FUNCTION)
		ERROR("Value of type '%s' is not callable\n",
				cb_value_type_friendly_name(func_val.type));

	func = func_val.val.as_function;
	name = func->name;
	num_opt_params = func->type == CB_FUNCTION_USER
		? func->value.as_user.num_opt_params
		: 0;
	if (func->arity - num_opt_params > num_args) {
		cb_str s = cb_agent_get_string(name);
		ERROR("Too few arguments to function '%s'\n", cb_strptr(&s));
	}
	if (func->type == CB_FUNCTION_NATIVE) {
		failed = func->value.as_native(num_args, sp - num_args,
				&result);
		assert(sp - frame->stack > (ssize_t) num_args);
		sp -= (num_args + 1);
		PUSH(result);
		if (failed) {
			retval = 1;
			RET_WITH_TRACE();
		}
	} else {
		struct cb_code *code = func->value.as_user.code;
		struct cb_value stack[code->stack_size + num_args];

		struct cb_frame next_frame;
		next_frame.is_function = 1;
		next_frame.num_args = num_args;
		next_frame.module = &cb_vm_state.modules[
			cb_modspec_id(code->modspec)];
		next_frame.code = code;
		next_frame.stack = stack;

		/* Copy the called function and arguments into the locals of
		   the next frame */
		memcpy(next_frame.stack, sp - num_args - 1,
				(num_args + 1) * sizeof(struct cb_value));
		next_frame.sp = NULL;
		if (cb_eval(&next_frame)) {
			retval = 1;
			RET_WITH_TRACE();
		}
		sp -= num_args;
		sp[-1] = next_frame.stack[0];
	}

	DISPATCH();
}

DO_OP_RETURN: {
	int i;
	struct cb_value retval;
	struct cb_upvalue *uv;

	retval = POP();

	/* close upvalues */
	if (cb_vm_state.upvalues_idx != 0) {
		for (i = cb_vm_state.upvalues_idx - 1; i >= 0; i -= 1) {
			uv = cb_vm_state.upvalues[i];
			/* XXX: is the <0 check actually needed? */
			if (!uv || uv->call_depth < 0 ||
					(size_t) uv->call_depth < cb_vm_state.call_depth)
				break;
			uv->call_depth = -1;
			uv->v.value = frame->stack[uv->v.idx];
		}
		cb_vm_state.upvalues_idx = i + 1;
	}

	sp = frame->stack;
	PUSH(retval);
	goto end;
}

DO_OP_POP:
	(void) POP();
	DISPATCH();

DO_OP_LOAD_LOCAL: {
	size_t idx = READ_SIZE_T();
	struct cb_value loc = LOCAL(idx);
	PUSH(loc);
	DISPATCH();
}

DO_OP_STORE_LOCAL: {
	size_t idx = READ_SIZE_T();
	struct cb_value val = TOP();
	REPLACE_LOCAL(idx, val);
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

DO_OP_BIND_LOCAL: {
	int i;
	struct cb_value func;
	struct cb_upvalue *uv;
	size_t dest_idx = READ_SIZE_T();
	size_t idx = LOCAL_IDX(READ_SIZE_T());

	func = POP();
	if (func.type != CB_VALUE_FUNCTION
			|| func.val.as_function->type != CB_FUNCTION_USER)
		ERROR("Can only bind upvalues to user functions");

	uv = NULL;
	for (i = cb_vm_state.upvalues_idx - 1; i >= 0; i -= 1) {
		struct cb_upvalue *uv2 = cb_vm_state.upvalues[i];
		if (uv2 == NULL || uv2->call_depth < 0
				|| (size_t) uv2->call_depth
					< cb_vm_state.call_depth)
			break;
		assert((size_t) uv2->call_depth == cb_vm_state.call_depth);
		if (uv2->v.idx == idx) {
			uv = uv2;
			break;
		}
	}

	if (!uv) {
		uv = malloc(sizeof(struct cb_upvalue));
		uv->refcount = 0;
		uv->call_depth = cb_vm_state.call_depth;
		uv->v.idx = idx;

		add_upvalue(uv);
	}

	cb_function_add_upvalue(&func.val.as_function->value.as_user, dest_idx,
			uv);

	PUSH(func);
	DISPATCH();
}

DO_OP_BIND_UPVALUE: {
	struct cb_value *self, *func;
	size_t dest_idx = READ_SIZE_T();
	size_t upvalue_idx = READ_SIZE_T();

	self = &frame->stack[0];
	assert(CB_VALUE_IS_USER_FN(self));
	func = &TOP();
	if (!CB_VALUE_IS_USER_FN(func))
		ERROR("Can only bind upvalues to user functions");

	cb_function_add_upvalue(&func->val.as_function->value.as_user, dest_idx,
		self->val.as_function->value.as_user.upvalues[upvalue_idx]);

	DISPATCH();
}

DO_OP_LOAD_UPVALUE: {
	struct cb_value *self, result;
	struct cb_upvalue *uv;
	size_t idx = READ_SIZE_T();

	self = &frame->stack[0];
	assert(CB_VALUE_IS_USER_FN(self));

	uv = self->val.as_function->value.as_user.upvalues[idx];
	result = cb_load_upvalue(uv);
	PUSH(result);

	DISPATCH();
}

DO_OP_STORE_UPVALUE: {
	struct cb_value *self;
	struct cb_upvalue *uv;
	size_t idx = READ_SIZE_T();

	self = &frame->stack[0];
	assert(CB_VALUE_IS_USER_FN(self));

	/* FIXME: Store cb_user_function in local */
	uv = self->val.as_function->value.as_user.upvalues[idx];
	store_upvalue(uv, TOP());

	DISPATCH();
}

DO_OP_LOAD_FROM_MODULE: {
	size_t mod_id, export_id, export_name;
	struct cb_module *mod;
	struct cb_value val;
	size_t idx;
	
	mod_id = READ_SIZE_T();
	export_id = READ_SIZE_T();
	mod = &cb_vm_state.modules[mod_id];

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

	memcpy(array->values, sp - size, size * sizeof(struct cb_value));
	sp -= size;

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

DO_OP_DUP: {
	struct cb_value top_val = TOP();
	PUSH(top_val);
	DISPATCH();
}

DO_OP_ALLOCATE_LOCALS: {
	size_t nlocals = READ_SIZE_T();
	/* CB_VALUE_NULL is 0, so we can just set the whole thing to 0 */
	memset(sp, 0, nlocals * sizeof(struct cb_value));
	sp += nlocals;
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

DO_OP_IMPORT_MODULE: {
	size_t const_id, mod_id;
	struct cb_const_module *const_mod;
	struct cb_module *mod;
	struct cb_code *code;
	int result;

	const_id = READ_SIZE_T();
	const_mod = consts[const_id].val.as_module;
	code = const_mod->code;
	mod_id = cb_modspec_id(const_mod->spec);
	mod = &cb_vm_state.modules[mod_id];
	if (mod->evaluated) {
		DISPATCH();
	}

	/* FIXME: Remove code that initializes modules ahead of time and just
	   do it here */
	if (cb_module_is_zero(*mod))
		init_module(mod, const_mod->spec);

	{
		struct cb_value stack[code->stack_size];

		struct cb_frame new_frame;
		new_frame.module = mod;
		new_frame.is_function = 0;
		new_frame.num_args = 0;
		new_frame.code = code;
		new_frame.stack = stack;
		new_frame.sp = NULL; /* pointer to local variable, assigned in cb_eval */

		result = cb_eval(&new_frame);
	}
	mod->evaluated = 1;
	if (result) {
		retval = 1;
		RET_WITH_TRACE();
	}

	DISPATCH();
}

DO_OP_APPLY_DEFAULT_ARG: {
	size_t param_num, next_param_addr;
	param_num = READ_SIZE_T();
	next_param_addr = READ_SIZE_T();
	if (frame->num_args > param_num)
		ip = frame->code->bytecode + next_param_addr;
	DISPATCH();
}
}

#undef CACHE
#undef DISPATCH
#undef ERROR
#undef LOCAL_IDX
#undef LOCAL
#undef REPLACE_LOCAL
#undef GLOBALS
#undef NEXT
#undef READ_SIZE_T
#undef TOP
}

#undef PUSH
#undef POP