#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "builtin_modules.h"
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
	/* Start with stack size 1 to avoid UB from applying offset to NULL */
	cb_vm_state.stack = malloc(sizeof(struct cb_value));
	cb_vm_state.stack_size = 1;

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

	if (cb_vm_state.stack)
		free(cb_vm_state.stack);
}

void cb_vm_grow_modules_array()
{
	size_t new_size = cb_agent_modspec_count() + 1;

	cb_vm_state.modules = realloc(cb_vm_state.modules,
			new_size * sizeof(struct cb_module));
	cb_module_zero(&cb_vm_state.modules[new_size - 1]);
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

CB_INLINE struct cb_value cb_load_upvalue(struct cb_upvalue *uv)
{
	if (uv->is_closed)
		return uv->v.value;

	return cb_vm_state.stack[uv->v.idx];
}

CB_INLINE void cb_store_upvalue(struct cb_upvalue *uv, struct cb_value val)
{
	if (uv->is_closed) {
		uv->v.value = val;
		return;
	}

	cb_vm_state.stack[uv->v.idx] = val;
}

static int cb_eval(struct cb_frame *frame);

void ensure_stack(size_t needed_size, size_t sp)
{
	if (cb_vm_state.stack_size < sp + needed_size) {
		cb_vm_state.stack_size = sp + needed_size;
		cb_vm_state.stack = realloc(cb_vm_state.stack,
				cb_vm_state.stack_size * sizeof(struct cb_value));
	}
}

int cb_run(struct cb_code *code)
{
	struct cb_frame frame;
	struct cb_module *module;
	int result;

	frame.module_id = cb_modspec_id(code->modspec);
	module = &cb_vm_state.modules[frame.module_id];
	if (cb_module_is_zero(*module))
		init_module(module, code->modspec);
	frame.is_function = 0;
	frame.num_args = 0;
	frame.code = code;

	size_t sp = cb_vm_state.frame && cb_vm_state.frame->sp
		? *cb_vm_state.frame->sp - cb_vm_state.stack : 0;
	frame.bp = sp;

	ensure_stack(code->stack_size, sp);
	result = cb_eval(&frame);
	/* don't use the module pointer from before; it might have moved */
	cb_vm_state.modules[frame.module_id].evaluated = 1;
	return result;
}

static CB_INLINE int do_call(unsigned short num_args, struct cb_frame *frame,
		struct cb_value **stack, struct cb_value **bp,
		struct cb_value **sp)
{
	struct cb_value func_val;
	struct cb_function *func;
	int failed;

	func_val = *(*sp - num_args - 1);

	if (func_val.type != CB_VALUE_FUNCTION) {
		struct cb_value err;
		cb_value_from_fmt(&err, "Value of type '%s' is not callable\n",
				cb_value_type_friendly_name(func_val.type));
		cb_error_set(err);
		return 1;
	}

	func = func_val.val.as_function;
	if (func->arity > num_args) {
		cb_str s = cb_agent_get_string(func->name);
		struct cb_value err;
		cb_value_from_fmt(&err, "Too few arguments to function '%s'\n",
				cb_strptr(&s));
		cb_error_set(err);
		return 1;
	}

	size_t old_sp = *sp - *stack;
	struct cb_frame next_frame;
	next_frame.is_function = 1;
	next_frame.num_args = num_args;
	next_frame.bp = old_sp - num_args - 1;

	if (func->type == CB_FUNCTION_NATIVE) {
		/* FIXME: somehow get module ID of native func */
		next_frame.module_id = (size_t) -1;
		next_frame.code = NULL;
		next_frame.is_native = 1;
		next_frame.sp = cb_vm_state.frame->sp;
		next_frame.parent = cb_vm_state.frame;
		cb_vm_state.frame = &next_frame;

		struct cb_value result;
		size_t dest = *sp - *stack - num_args - 1;
		failed = func->value.as_native(num_args, *sp - num_args,
				&result);
		cb_vm_state.stack[dest] = result;
		cb_vm_state.frame = next_frame.parent;
	} else {
		struct cb_code *code = func->value.as_user.code;

		next_frame.module_id = cb_modspec_id(code->modspec);
		next_frame.code = code;
		next_frame.is_native = 0;

		ensure_stack(code->stack_size, old_sp);
		failed = cb_eval(&next_frame);
	}

	*stack = cb_vm_state.stack;
	*bp = *stack + frame->bp;
	*sp = *stack + old_sp - num_args;

	return failed;
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
		struct cb_value err;
		cb_value_from_fmt(&err, "Value of type '%s' is not callable",
				cb_value_type_friendly_name(fn.type));
		cb_error_set(err);
		return -1;
	}

	if (fn.val.as_function->type == CB_FUNCTION_USER) {
		struct cb_module *module;

		user_func = &fn.val.as_function->value.as_user;

		/* +1 for the function itself */
		stack_size = user_func->code->stack_size + args_len + 1;
		size_t bp = cb_vm_state.frame ?
			*cb_vm_state.frame->sp - cb_vm_state.stack : 0;
		ensure_stack(stack_size, bp);

		cb_vm_state.stack[bp] = fn;
		memcpy(&cb_vm_state.stack[bp + 1], args,
				args_len * sizeof(struct cb_value));

		frame.is_function = 1;
		frame.num_args = args_len;
		frame.code = user_func->code;
		frame.module_id = cb_modspec_id(user_func->code->modspec);
		frame.bp = bp;
		module = &cb_vm_state.modules[frame.module_id];
		if (cb_module_is_zero(*module))
			init_module(module, user_func->code->modspec);

		ret = cb_eval(&frame);
		/* the result replaces the function on the stack */
		*result = cb_vm_state.stack[bp];
	} else {
		native_func = fn.val.as_function->value.as_native;
		ret = native_func(args_len, args, result);
	}
	return ret;
}

#ifdef CB_DEBUG_VM
static void debug_state(size_t sp, size_t pc, struct cb_frame *frame)
{
	cb_str modname_str, funcname_str;
	char *modname = "script";
	char *funcname;

	if (frame->module_id) {
		struct cb_module *module = &cb_vm_state.modules[frame->module_id];
		modname_str = cb_agent_get_string(cb_modspec_name( module->spec));
		modname = cb_strptr(&modname_str);
	}

	if (!frame->is_function) {
		funcname = "top";
	} else {
		struct cb_value func = cb_vm_state.stack[frame->bp];
		assert(func.type == CB_VALUE_FUNCTION);
		size_t name = func.val.as_function->name;
		funcname_str = cb_agent_get_string(name);
		funcname = cb_strptr(&funcname_str);
	}

	printf("%s%s%s\n", modname, !frame->is_function ? " " : ".", funcname);
	printf("pc: %zu, sp: %zu, bp: %zu\n", pc, sp, frame->bp);
	cb_disassemble_one(frame->code->bytecode[pc], pc);
	printf("> %s", sp ? (sp > 10 ? "... " : "") : "(empty)");
	int _first = 1;
	for (int _i = (10 < sp ? 10 : sp); _i > 0; _i -= 1) {
		int _idx = sp - _i;
		if (_first)
			_first = 0;
		else
			printf(", ");
		cb_str _str = cb_value_to_string(cb_vm_state.stack[_idx]);
		printf("%s", cb_strptr(&_str));
		cb_str_free(_str);
	}
	printf("\n\n");
}
#endif

/* FIXME: hacky as */
static int method_caller(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value receiver, func;
	struct cb_frame *frame, new_frame;
	struct cb_user_function *ufunc;
	struct cb_code *code;

	receiver = cb_cfunc_load_upvalue(0);
	func = cb_cfunc_load_upvalue(1);
	assert(func.val.as_function->type == CB_FUNCTION_USER);
	ufunc = &func.val.as_function->value.as_user;
	code = ufunc->code;

	frame = cb_vm_state.frame;
	ensure_stack(code->stack_size + 1, *frame->sp - cb_vm_state.stack);
	memmove(argv, argv + 1, argc);

	cb_vm_state.stack[frame->bp] = receiver;
	cb_vm_state.stack[frame->bp + 1] = func;

	new_frame.parent = frame;
	new_frame.module_id = cb_modspec_id(code->modspec);
	new_frame.is_function = 1;
	new_frame.is_native = 0;
	new_frame.num_args = argc;
	new_frame.code = code;
	new_frame.bp = frame->bp + 1;

	int failed = cb_eval(&new_frame);
	cb_vm_state.frame = frame;
	*result = cb_vm_state.stack[new_frame.bp];
	return failed;
}

static void set_upvalue(struct cb_upvalue **uv, struct cb_value value)
{
	(*uv) = malloc(sizeof(struct cb_upvalue));
	(*uv)->refcount = 1;
	(*uv)->is_closed = 1;
	(*uv)->v.value = value;
}

static struct cb_value make_method_caller(struct cb_value receiver,
		struct cb_function *method)
{
	struct cb_value bound_method = cb_cfunc_new(method->name, method->arity,
			method_caller);
	const int num_upvalues = 2;
	struct cb_upvalue **method_upvalues = malloc(
			num_upvalues * sizeof(struct cb_upvalue *));
	bound_method.val.as_function->nupvalues = num_upvalues;
	bound_method.val.as_function->upvalues = method_upvalues;

	set_upvalue(&method_upvalues[0], receiver);
	set_upvalue(&method_upvalues[1], (struct cb_value) {
		.type = CB_VALUE_FUNCTION,
		.val = { .as_function = method },
	});

	return bound_method;
}

int eval_depth = 0;

static int cb_eval(struct cb_frame *frame)
{
#define TABLE_ENTRY(OP) &&DO_##OP,
	static const void *dispatch_table[] = {
		CB_OPCODE_LIST(TABLE_ENTRY)
	};
#undef TABLE_ENTRY

#ifdef CB_DEBUG_VM
# define DEBUG_STATE() \
	debug_state(sp - stack, ip - frame->code->bytecode, frame)
#else
# define DEBUG_STATE()
#endif

#define NEXT() (*ip++)
#define DISPATCH() ({ \
		DEBUG_STATE(); \
		op.as_size_t = NEXT(); \
		assert(op.unary.op < OP_MAX); \
		goto *dispatch_table[op.unary.op]; \
	})
#define ARG (op.unary.arg)
#define ARG1 (op.binary.arg1)
#define ARG2 (op.binary.arg2)

#define RET_WITH_TRACE() ({ \
		cb_traceback_add_frame(frame, ip - 1 - bytecode); \
		if (try_stack != try_stack_base) { \
			ip = bytecode + try_stack[-1]; \
			retval = 0; \
			DISPATCH(); \
		} else { \
			goto end; \
		} \
	})
#define ERROR(MSG, ...) ({ \
		struct cb_value err; \
		cb_value_from_fmt(&err, (MSG), ##__VA_ARGS__); \
		cb_error_set(err); \
		retval = 1; \
		RET_WITH_TRACE(); \
	})
#define PEEK(N) (sp[-(N + 1)])
#define TOP() PEEK(0)
#define LOCAL_IDX(N) (bp - stack + 1 + (N))
#define LOCAL(N) (bp[N + 1])
#define REPLACE_LOCAL(N, VAL) ({ \
		struct cb_value _v = (VAL); \
		bp[N + 1] = _v; \
	})
#define GLOBALS() (global_scope)
#define PUSH(V) (*sp++ = (V))
#define POP() (*--sp)
#define CACHE() (inline_cache[ip - bytecode - 1])

	assert(cb_vm_state.stack);
	struct cb_value *stack = cb_vm_state.stack;
	struct cb_value *bp = stack + frame->bp;
	struct cb_value *sp = bp + frame->is_function + frame->num_args;
	cb_instruction *bytecode = frame->code->bytecode;
	cb_instruction *ip = bytecode;
	int retval = 0;
	struct cb_const *consts = frame->code->const_pool;
	cb_hashmap *global_scope = cb_vm_state.modules[frame->module_id].global_scope;
	union cb_inline_cache *inline_cache = frame->code->ic;
	size_t *try_stack = alloca(frame->code->max_try_depth * sizeof(size_t));
	size_t *const try_stack_base = try_stack;

	union cb_op_encoding op;

	frame->sp = &sp;
	frame->parent = cb_vm_state.frame;
	cb_vm_state.frame = frame;

	if (++eval_depth >= 1000) {
		ERROR("Stack overflow");
	}

	DISPATCH();

DO_OP_MAX:
DO_OP_HALT:
end:
	eval_depth -= 1;
	cb_vm_state.frame = cb_vm_state.frame->parent;
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
	struct cb_const cst = consts[ARG];
	struct cb_value as_value = cb_const_to_value(&cst);
	PUSH(as_value);
	DISPATCH();
}

DO_OP_CONST_STRING: {
	struct cb_value val;
	val.type = CB_VALUE_INTERNED_STRING;
	val.val.as_interned_string = ARG;
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

#define NUMBER_BINOP(OP, INT_OP, B) ({ \
		struct cb_value a, b, result; \
		b = (B); \
		a = POP(); \
		if (a.type == CB_VALUE_INT && b.type == CB_VALUE_INT) { \
			result.type = CB_VALUE_INT; \
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

DO_OP_INC:
	NUMBER_BINOP(+, __builtin_add_overflow, cb_int(1));
	DISPATCH();

DO_OP_DEC:
	NUMBER_BINOP(-, __builtin_sub_overflow, cb_int(1));
	DISPATCH();

DO_OP_ADD:
	NUMBER_BINOP(+, __builtin_add_overflow, POP());
	DISPATCH();

DO_OP_SUB:
	NUMBER_BINOP(-, __builtin_sub_overflow, POP());
	DISPATCH();

DO_OP_MUL:
	NUMBER_BINOP(*, __builtin_mul_overflow, POP());
	DISPATCH();

DO_OP_DIV:
#define INT_DIV(a, b, result) ((*(result) = (a) / (b)), 0)
	NUMBER_BINOP(/, INT_DIV, POP());
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
	size_t dest = ARG;
	/* XXX: JUMP macro */
	ip = bytecode + dest;
	DISPATCH();
}

DO_OP_JUMP_IF_TRUE: {
	struct cb_value pred;
	size_t next = ARG;
	pred = POP();
	if (cb_value_is_truthy(&pred))
		ip = bytecode + next;
	DISPATCH();
}

DO_OP_JUMP_IF_FALSE: {
	struct cb_value pred;
	size_t next = ARG;
	pred = POP();
	if (!cb_value_is_truthy(&pred))
		ip = bytecode + next;
	DISPATCH();
}

DO_OP_CALL_METHOD:
	if (do_call(ARG, frame, &stack, &bp, &sp)) {
		retval = 1;
		RET_WITH_TRACE();
	}

	PEEK(1) = PEEK(0);
	(void) POP();

	DISPATCH();

DO_OP_CALL:
	if (do_call(ARG, frame, &stack, &bp, &sp)) {
		retval = 1;
		RET_WITH_TRACE();
	}

	DISPATCH();

DO_OP_RETURN: {
	int i;
	struct cb_value retval;
	struct cb_upvalue *uv;

	retval = POP();

	/* close upvalues */
	for (i = cb_vm_state.upvalues_idx - 1; i >= 0; i -= 1) {
		uv = cb_vm_state.upvalues[i];
		assert(uv);
		assert(!uv->is_closed);
		if (uv->v.idx < frame->bp)
			break;
		uv->is_closed = 1;
		uv->v.value = stack[uv->v.idx];
	}
	cb_vm_state.upvalues_idx = i + 1;

	sp = bp;
	PUSH(retval);
	goto end;
}

DO_OP_POP:
	(void) POP();
	DISPATCH();

DO_OP_LOAD_LOCAL: {
	size_t idx = ARG;
	struct cb_value loc = LOCAL(idx);
	PUSH(loc);
	DISPATCH();
}

DO_OP_STORE_LOCAL: {
	size_t idx = ARG;
	struct cb_value val = TOP();
	REPLACE_LOCAL(idx, val);
	DISPATCH();
}

DO_OP_LOAD_GLOBAL: {
	size_t id;
	struct cb_value value;

	id = ARG;

	struct cb_load_global_cache *ic = &CACHE().load_global;
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
		ic->version = cb_hashmap_version(GLOBALS());
		ic->index = idx;
	}

	PUSH(value);
	DISPATCH();
}

DO_OP_DECLARE_GLOBAL: {
	size_t id;
	struct cb_value null_value;

	id = ARG;
	null_value.type = CB_VALUE_NULL;

	cb_hashmap_set(GLOBALS(), id, null_value);
	DISPATCH();
}

DO_OP_STORE_GLOBAL: {
	size_t id;

	id = ARG;

	struct cb_load_global_cache *ic = &CACHE().load_global;
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
	size_t dest_idx = ARG1;
	size_t idx = LOCAL_IDX(ARG2);

	func = POP();
	if (func.type != CB_VALUE_FUNCTION
			|| func.val.as_function->type != CB_FUNCTION_USER)
		ERROR("Can only bind upvalues to user functions");

	uv = NULL;
	for (i = cb_vm_state.upvalues_idx - 1; i >= 0; i -= 1) {
		struct cb_upvalue *uv2 = cb_vm_state.upvalues[i];
		assert(uv2);
		assert(!uv2->is_closed);
		if (uv2->v.idx < idx)
			break;
		else if (uv2->v.idx == idx) {
			uv = uv2;
			break;
		}
	}

	if (!uv) {
		uv = malloc(sizeof(struct cb_upvalue));
		uv->refcount = 0;
		uv->is_closed = 0;
		uv->v.idx = idx;

		add_upvalue(uv);
	}

	cb_function_add_upvalue(func.val.as_function, dest_idx, uv);

	PUSH(func);
	DISPATCH();
}

DO_OP_BIND_UPVALUE: {
	struct cb_value *self, *func;
	size_t dest_idx = ARG1;
	size_t upvalue_idx = ARG2;

	self = bp;
	assert(CB_VALUE_IS_USER_FN(self));
	func = &TOP();
	if (!CB_VALUE_IS_USER_FN(func))
		ERROR("Can only bind upvalues to user functions");

	cb_function_add_upvalue(func->val.as_function, dest_idx,
		self->val.as_function->upvalues[upvalue_idx]);

	DISPATCH();
}

DO_OP_LOAD_UPVALUE: {
	struct cb_value *self, result;
	struct cb_upvalue *uv;
	size_t idx = ARG;

	self = bp;
	assert(CB_VALUE_IS_USER_FN(self));

	uv = self->val.as_function->upvalues[idx];
	result = cb_load_upvalue(uv);
	PUSH(result);

	DISPATCH();
}

DO_OP_STORE_UPVALUE: {
	struct cb_value *self;
	struct cb_upvalue *uv;
	size_t idx = ARG;

	self = bp;
	assert(CB_VALUE_IS_USER_FN(self));

	/* FIXME: Store self cb_user_function in local */
	uv = self->val.as_function->upvalues[idx];
	cb_store_upvalue(uv, TOP());

	DISPATCH();
}

DO_OP_LOAD_FROM_MODULE: {
	size_t mod_id, export_id, export_name;
	struct cb_module *mod;
	struct cb_value val;
	size_t idx;
	
	mod_id = ARG1;
	export_id = ARG2;
	mod = &cb_vm_state.modules[mod_id];

	struct cb_load_from_module_cache *ic = &CACHE().load_from_module;

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
	size_t size = ARG;
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
		else if (v.val.as_int < 0) \
			ERROR("Array index must be positive or 0."); \
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

DO_OP_DUP_2: {
	struct cb_value a, b;
	a = PEEK(0);
	b = PEEK(1);
	PUSH(b);
	PUSH(a);
	DISPATCH();
}

DO_OP_ALLOCATE_LOCALS: {
	size_t nlocals = ARG;
	/* CB_VALUE_NULL is 0, so we can just set the whole thing to 0 */
	memset(sp, 0, nlocals * sizeof(struct cb_value));
	sp += nlocals;
	DISPATCH();
}

DO_OP_SET_METHOD: {
	size_t index = ARG1;
	size_t method_name = ARG2;
	struct cb_value spec, method;

	spec = POP();
	method = POP();

	assert(spec.type == CB_VALUE_STRUCT_SPEC);
	assert(method.type == CB_VALUE_FUNCTION);

	cb_struct_spec_set_method(spec.val.as_struct_spec, index, method_name,
			method.val.as_function);

	PUSH(spec);
	DISPATCH();
}

DO_OP_LOAD_METHOD: {
	size_t method_name = ARG;
	struct cb_value recv = TOP();
	if (recv.type != CB_VALUE_STRUCT)
		ERROR("Cannot get method of non-struct type %s",
				cb_value_type_friendly_name(recv.type));

	// TODO: inline cache?
	struct cb_struct *s = recv.val.as_struct;
	struct cb_struct_spec *spec = s->spec;
	struct cb_function *method = cb_struct_spec_get_method(spec, method_name);

	struct cb_value funcval;
	if (method) {
		funcval.type = CB_VALUE_FUNCTION;
		funcval.val.as_function = method;
	} else {
		struct cb_value *field = cb_struct_get_field(s, method_name, NULL);
		if (!field) {
			cb_str spec_name_str, method_name_str;
			spec_name_str = cb_agent_get_string(spec->name);
			method_name_str = cb_agent_get_string(method_name);
			ERROR("%s struct has no method %s",
					cb_strptr(&spec_name_str),
					cb_strptr(&method_name_str));
		}
		funcval = *field;
	}

	PUSH(funcval);
	DISPATCH();
}

DO_OP_LOAD_THIS:
	assert(bp > cb_vm_state.stack);
	PUSH(bp[-1]);
	DISPATCH();

DO_OP_LOAD_STRUCT: {
	size_t fname = ARG;
	struct cb_value recv = POP();
	if (recv.type != CB_VALUE_STRUCT)
		ERROR("Cannot get field of non-struct type %s",
				cb_value_type_friendly_name(recv.type));
	struct cb_struct *s = recv.val.as_struct;

	struct cb_load_struct_cache *ic = &CACHE().load_struct;

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
	struct cb_value result;
	if (val) {
		result = *val;
	} else {
		struct cb_function *method =
			cb_struct_spec_get_method(s->spec, fname);
		if (method == NULL) {
			cb_str fname_str, specname_str;

			fname_str = cb_agent_get_string(fname);
			specname_str = cb_agent_get_string(s->spec->name);
			ERROR("No such field '%s' on struct '%s'",
					cb_strptr(&fname_str),
					cb_strptr(&specname_str));
		}
		result = make_method_caller(recv, method);
	}
	if (ic->index != -1) {
		ic->spec = s->spec;
		ic->index = idx;
	}
	PUSH(result);
	DISPATCH();
}

#define DO_STORE_STRUCT_FIELD(RET) ({ \
	size_t fname = ARG; \
	struct cb_value val = POP(); \
	struct cb_value recv = POP(); \
	if (recv.type != CB_VALUE_STRUCT) \
		ERROR("Cannot set field of non-struct type %s", \
				cb_value_type_friendly_name(recv.type)); \
	struct cb_struct *s = recv.val.as_struct; \
	struct cb_load_struct_cache *ic = &CACHE().load_struct; \
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

DO_OP_ROT_2: {
	struct cb_value a, b;
	a = POP();
	b = POP();
	PUSH(a);
	PUSH(b);
	DISPATCH();
}

DO_OP_ROT_3: {
	struct cb_value a, b, c;
	// C, B, A
	// A, C, B
	a = POP();
	b = POP();
	c = POP();
	PUSH(a);
	PUSH(c);
	PUSH(b);
	DISPATCH();
}

DO_OP_ROT_4: {
	struct cb_value a, b, c, d;
	// D, C, B, A
	// A, D, C, B
	a = POP();
	b = POP();
	c = POP();
	d = POP();
	PUSH(a);
	PUSH(d);
	PUSH(c);
	PUSH(b);
	DISPATCH();
}

DO_OP_IMPORT_MODULE: {
	size_t const_id, mod_id;
	cb_modspec *modspec;
	struct cb_module *mod;
	struct cb_code *code;
	int failed;

	const_id = ARG;
	assert(consts[const_id].type == CB_CONST_MODULE);
	modspec = consts[const_id].val.as_module;
	code = cb_modspec_code(modspec);
	mod_id = cb_modspec_id(modspec);
	mod = &cb_vm_state.modules[mod_id];
	if (mod->evaluated) {
		DISPATCH();
	}

	/* FIXME: Remove code that initializes modules ahead of time and just
	   do it here */
	if (cb_module_is_zero(*mod))
		init_module(mod, modspec);

	struct cb_frame new_frame;
	new_frame.module_id = mod_id;
	new_frame.is_function = 0;
	new_frame.num_args = 0;
	new_frame.code = code;
	new_frame.bp = sp - stack;

	ensure_stack(code->stack_size, sp - stack);
	failed = cb_eval(&new_frame);
	stack = cb_vm_state.stack;
	bp = stack + frame->bp;
	sp = stack + new_frame.bp;

	mod->evaluated = 1;
	if (failed) {
		retval = 1;
		RET_WITH_TRACE();
	}

	DISPATCH();
}

DO_OP_APPLY_DEFAULT_ARG: {
	size_t param_num, next_param_addr;
	param_num = ARG1;
	next_param_addr = ARG2;
	if (frame->num_args > param_num)
		ip = bytecode + next_param_addr;
	DISPATCH();

DO_OP_THROW: {
	struct cb_value err;

	err = POP();
	cb_error_set(err);
	retval = 1;

	RET_WITH_TRACE();
}

DO_OP_PUSH_TRY:
	*try_stack++ = ARG;
	DISPATCH();

DO_OP_POP_TRY:
	try_stack--;
	DISPATCH();

DO_OP_CATCH: {
	struct cb_value err;
	try_stack--;
	assert(cb_error_p());
	err = cb_error_value();
	cb_error_recover();
	PUSH(err);
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
#undef TOP
#undef ARG
#undef ARG1
#undef ARG2
}

#undef PUSH
#undef POP