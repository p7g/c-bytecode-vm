#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "builtin_modules.h"
#include "cbcvm.h"
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
#define STACK_INIT_SIZE 1024

void cb_vm_init(cb_bytecode *bytecode)
{
	cb_vm_state.bytecode = bytecode;

	cb_vm_state.sp = 0;
	cb_vm_state.stack = malloc(STACK_INIT_SIZE * sizeof(struct cb_value));
	cb_vm_state.stack_size = STACK_INIT_SIZE;

	cb_vm_state.upvalues = calloc(32, sizeof(struct cb_upvalue *));
	cb_vm_state.upvalues_idx = 0;
	cb_vm_state.upvalues_size = 32;

	cb_vm_state.modules = calloc(cb_agent_modspec_count(),
			sizeof(struct cb_module));

	cb_instantiate_builtin_modules();
}

void cb_vm_deinit(void)
{
	int i;
	size_t num_modules = cb_agent_modspec_count();

	for (i = 0; i < num_modules; i += 1) {
		if (!cb_module_is_zero(cb_vm_state.modules[i]))
			cb_module_free(&cb_vm_state.modules[i]);
	}
	free(cb_vm_state.modules);
	cb_vm_state.modules = NULL;

	free(cb_vm_state.upvalues);
	cb_vm_state.upvalues = NULL;
	free(cb_vm_state.stack);
	cb_vm_state.sp = 0;
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

int cb_eval(size_t pc, struct cb_frame *frame);

int cb_run(void)
{
	struct cb_frame frame;

	frame.parent = NULL;
	frame.module = NULL;
	frame.bp = 0;
	frame.is_function = 0;

	return cb_eval(0, &frame);
}

#define PUSH(V) ({ \
		struct cb_value _v = (V); \
		if (cb_vm_state.sp >= cb_vm_state.stack_size) { \
			cb_vm_state.stack_size <<= 2; \
			cb_vm_state.stack = realloc(cb_vm_state.stack, \
					cb_vm_state.stack_size \
					* sizeof(struct cb_value)); \
		} \
		cb_vm_state.stack[cb_vm_state.sp++] = _v; \
	})
#define POP() ({ \
		assert(cb_vm_state.sp > 0); \
		struct cb_value _v = cb_vm_state.stack[--cb_vm_state.sp]; \
		_v; \
	})

int cb_vm_call_user_func(struct cb_value fn, struct cb_value *args,
		size_t args_len, struct cb_value *result)
{
	int i, ret;
	struct cb_frame frame;
	struct cb_user_function *func;

	assert(CB_VALUE_IS_USER_FN(&fn));
	func = &fn.val.as_function->value.as_user;

	frame.parent = cb_vm_state.frame;
	frame.is_function = 1;
	frame.bp = cb_vm_state.sp;
	PUSH(fn);

	for (i = 0; i < args_len; i += 1)
		PUSH(args[i]);

	if (func->module_id != -1)
		frame.module = &cb_vm_state.modules[func->module_id];
	else
		frame.module = NULL;

	ret = cb_eval(func->address, &frame);
	*result = POP();
	return ret;
}

static void debug_state(cb_bytecode *bytecode, size_t pc, struct cb_frame *frame)
{
	size_t _name;
	printf("%s%s%s\n", frame->module
			? cb_strptr(cb_agent_get_string(
					cb_modspec_name(frame->module->spec)))
			: "script",
			!frame->is_function ? " " : ".",
			!frame->is_function ? "top"
			: (_name = cb_vm_state.stack[frame->bp]
				.val.as_function->name) != -1
			? cb_strptr(cb_agent_get_string(_name))
			: "<anonymous>");
	printf("pc: %zu, bp: %zu, sp: %zu\n", pc, frame->bp, cb_vm_state.sp);
	cb_disassemble_one(bytecode, pc);
	printf("> %s", cb_vm_state.sp ? cb_vm_state.sp > 10
			? "... "
			: "" : "(empty)");
	int _first = 1;
	for (int _i = 10 < cb_vm_state.sp ? 10 : cb_vm_state.sp;
			_i > 0; _i -= 1) {
		int _idx = cb_vm_state.sp - _i;
		if (_first)
			_first = 0;
		else
			printf(", ");
		char *_str = cb_value_to_string(&cb_vm_state.stack[_idx]);
		printf("%s", _str);
		free(_str);
	}
	printf("\n\n");
}

int cb_eval(size_t pc, struct cb_frame *frame)
{
	int retval = 0;
	cb_vm_state.frame = frame;

#define TABLE_ENTRY(OP) &&DO_##OP,
	static void *dispatch_table[] = {
		CB_OPCODE_LIST(TABLE_ENTRY)
	};
#undef TABLE_ENTRY

#define NEXT() (cb_bytecode_get(cb_vm_state.bytecode, pc++))
# define DISPATCH() ({ \
		if (cb_options.debug_vm) \
			debug_state(cb_vm_state.bytecode, pc, frame); \
		size_t _next = NEXT(); \
		assert(_next >= 0 && _next < OP_MAX); \
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
#define READ_SIZE_T() ({ \
		int _i = 0; \
		size_t _val = 0; \
		for (_i = 0; _i < sizeof(size_t) / sizeof(cb_instruction); _i += 1) \
			_val += ((size_t) NEXT()) << (_i * 8 * sizeof(cb_instruction)); \
		_val; \
	})
#define TOP() (cb_vm_state.stack[cb_vm_state.sp - 1])
#define FRAME() (frame)
#define LOCAL_IDX(N) (frame->bp + 1 + (N))
#define LOCAL(N) ({ \
		struct cb_value _v = cb_vm_state.stack[LOCAL_IDX(N)]; \
		_v; \
	})
#define REPLACE(N, VAL) ({ \
		struct cb_value _v = (VAL); \
		cb_vm_state.stack[(N)] = _v; \
	})
#define GLOBALS() (frame->module->global_scope)

	DISPATCH();

DO_OP_MAX:
DO_OP_HALT:
end:
	cb_vm_state.frame = cb_vm_state.frame->parent;
	if (retval && !cb_vm_state.frame) {
		fprintf(stderr, "Traceback (most recent call last):\n");
		struct cb_traceback *tb;
		for (tb = cb_error_tb(); tb; tb = tb->next)
			cb_traceback_print(stderr, tb);
		char *as_str = cb_value_to_string(cb_error_value());
		fprintf(stderr, "Uncaught error: %s\n", as_str);
		free(as_str);
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

DO_OP_JUMP:
	pc = READ_SIZE_T();
	DISPATCH();

DO_OP_JUMP_IF_TRUE: {
	struct cb_value pred;
	size_t next = READ_SIZE_T();
	pred = POP();
	if ((pred.type == CB_VALUE_BOOL && pred.val.as_bool)
			|| cb_value_is_truthy(&pred))
		pc = next;
	DISPATCH();
}

DO_OP_JUMP_IF_FALSE: {
	struct cb_value pred;
	size_t next = READ_SIZE_T();
	pred = POP();
	if ((pred.type == CB_VALUE_BOOL && !pred.val.as_bool)
			|| !cb_value_is_truthy(&pred))
		pc = next;
	DISPATCH();
}

DO_OP_CALL: {
	size_t num_args, name;
	struct cb_value func_val, result;
	struct cb_function *func;
	struct cb_frame next_frame;
	int failed;

	num_args = READ_SIZE_T();
	func_val = cb_vm_state.stack[cb_vm_state.sp - num_args - 1];

	if (func_val.type != CB_VALUE_FUNCTION)
		ERROR("Value of type '%s' is not callable\n",
				cb_value_type_friendly_name(func_val.type));

	func = func_val.val.as_function;
	name = func->name;
	if (func->arity > num_args)
		ERROR("Too few arguments to function '%s'\n",
				func->name == (size_t) -1
				? "<anonymous>"
				: cb_strptr(cb_agent_get_string(name)));
	if (func->type == CB_FUNCTION_NATIVE) {
		failed = func->value.as_native(num_args,
				&cb_vm_state.stack[cb_vm_state.sp - num_args],
					&result);
		assert(cb_vm_state.sp > num_args);
		cb_vm_state.sp -= (num_args + 1);
		PUSH(result);
		if (failed) {
			retval = 1;
			RET_WITH_TRACE();
		}
	} else {
		next_frame.parent = frame;
		next_frame.is_function = 1;
		next_frame.bp = cb_vm_state.sp - num_args - 1;
		if (func->value.as_user.module_id != -1)
			next_frame.module = &cb_vm_state.modules[
				func->value.as_user.module_id];
		else
			next_frame.module = NULL;
		if (cb_eval(cb_ufunc_entry(func, num_args), &next_frame)) {
			retval = 1;
			RET_WITH_TRACE();
		}
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
			if (!uv || !uv->is_open || uv->v.idx < frame->bp)
				break;
			uv->is_open = 0;
			uv->v.value = cb_vm_state.stack[uv->v.idx];
		}
		cb_vm_state.upvalues_idx = i + 1;
	}

	cb_vm_state.sp = frame->bp;
	PUSH(retval);
	goto end;
}

DO_OP_POP:
	POP();
	DISPATCH();

DO_OP_LOAD_LOCAL:
	PUSH(LOCAL(READ_SIZE_T()));
	DISPATCH();

DO_OP_STORE_LOCAL:
	REPLACE(LOCAL_IDX(READ_SIZE_T()), TOP());
	DISPATCH();

DO_OP_LOAD_GLOBAL: {
	size_t id;
	struct cb_value *value;

	id = READ_SIZE_T();
	value = cb_hashmap_get(GLOBALS(), id);

	if (value == NULL)
		ERROR("Unbound global '%s'",
				cb_strptr(cb_agent_get_string(id)));

	PUSH(*value);
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
	cb_hashmap_set(GLOBALS(), id, TOP());
	DISPATCH();
}

DO_OP_NEW_FUNCTION: {
	size_t name, arity, address, nopt, i;
	struct cb_function *func;
	struct cb_value func_val;

	name = READ_SIZE_T();
	arity = READ_SIZE_T();
	address = READ_SIZE_T();
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
			: -1,
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
	struct cb_value func;
	struct cb_upvalue *uv;
	size_t idx = LOCAL_IDX(READ_SIZE_T());

	func = POP();
	if (func.type != CB_VALUE_FUNCTION
			|| func.val.as_function->type != CB_FUNCTION_USER)
		ERROR("Can only bind upvalues to user functions");

	uv = NULL;
	for (i = cb_vm_state.upvalues_idx - 1; i >= 0; i -= 1) {
		if (cb_vm_state.upvalues[i] == NULL
				|| !cb_vm_state.upvalues[i]->is_open
				|| cb_vm_state.upvalues[i]->v.idx < frame->bp)
			break;
		if (cb_vm_state.upvalues[i]->v.idx == idx) {
			uv = cb_vm_state.upvalues[i];
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

	cb_function_add_upvalue(&func.val.as_function->value.as_user, uv);

	PUSH(func);
	DISPATCH();
}

DO_OP_BIND_UPVALUE: {
	struct cb_value *self, func;
	size_t upvalue_idx = READ_SIZE_T();

	self = &cb_vm_state.stack[frame->bp];
	assert(CB_VALUE_IS_USER_FN(self));
	func = POP();
	if (!CB_VALUE_IS_USER_FN(&func))
		ERROR("Can only bind upvalues to user functions");

	cb_function_add_upvalue(&func.val.as_function->value.as_user,
		self->val.as_function->value.as_user.upvalues[upvalue_idx]);

	PUSH(func);
	DISPATCH();
}

DO_OP_LOAD_UPVALUE: {
	struct cb_value *self;
	struct cb_upvalue *uv;
	size_t idx = READ_SIZE_T();

	self = &cb_vm_state.stack[frame->bp];
	assert(CB_VALUE_IS_USER_FN(self));

	uv = self->val.as_function->value.as_user.upvalues[idx];
	if (uv->is_open)
		PUSH(cb_vm_state.stack[uv->v.idx]);
	else
		PUSH(uv->v.value);

	DISPATCH();
}

DO_OP_STORE_UPVALUE: {
	struct cb_value *self;
	struct cb_upvalue *uv;
	size_t idx = READ_SIZE_T();

	self = &cb_vm_state.stack[frame->bp];
	assert(CB_VALUE_IS_USER_FN(self));

	uv = self->val.as_function->value.as_user.upvalues[idx];
	if (uv->is_open)
		cb_vm_state.stack[uv->v.idx] = TOP();
	else
		uv->v.value = TOP();

	DISPATCH();
}

DO_OP_LOAD_FROM_MODULE: {
	size_t mod_id, export_id, export_name;
	struct cb_module *mod;
	struct cb_value *val;
	
	mod_id = READ_SIZE_T();
	export_id = READ_SIZE_T();
	mod = &cb_vm_state.modules[mod_id];
	export_name = cb_modspec_get_export_name(mod->spec, export_id);
	val = cb_hashmap_get(mod->global_scope, export_name);
	assert(val != NULL);
	PUSH(*val);
	DISPATCH();
}

DO_OP_EXPORT:
	/* FIXME: use export IDs rather than hashmap lookups */
	READ_SIZE_T();
	POP();
	DISPATCH();

DO_OP_NEW_ARRAY_WITH_VALUES: {
	struct cb_value arrayval;
	size_t size = READ_SIZE_T();
	struct cb_array *array = cb_array_new(size);
	array->len = size;
	for (int i = size - 1; i >= 0; i -= 1)
		array->values[i] = POP();
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
	make_intrinsics(mod.global_scope);
	cb_vm_state.modules[module_id] = mod;
	frame->module = &cb_vm_state.modules[module_id];

	DISPATCH();
}

DO_OP_ENTER_MODULE:
	frame->module = &cb_vm_state.modules[READ_SIZE_T()];
	DISPATCH();


DO_OP_END_MODULE:
DO_OP_EXIT_MODULE:
	frame->module = NULL;
	DISPATCH();

DO_OP_DUP: {
	struct cb_value val;

	val = POP();
	PUSH(val);
	PUSH(val);
	DISPATCH();
}

DO_OP_ALLOCATE_LOCALS: {
	int i;
	size_t nlocals = READ_SIZE_T();
	struct cb_value null_value;
	null_value.type = CB_VALUE_NULL;
	for (i = 0; i < nlocals; i += 1)
		PUSH(null_value);
	DISPATCH();
}

DO_OP_LOAD_STRUCT: {
	size_t fname = READ_SIZE_T();
	struct cb_value recv = POP();
	if (recv.type != CB_VALUE_STRUCT)
		ERROR("Cannot get field of non-struct type %s",
				cb_value_type_friendly_name(recv.type));
	struct cb_struct *s = recv.val.as_struct;
	struct cb_value *val = cb_struct_get_field(s, fname);
	if (!val) {
		ERROR("No such field '%s' on struct '%s'",
				cb_strptr(cb_agent_get_string(fname)),
				cb_strptr(cb_agent_get_string(s->spec->name)));
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
	if (cb_struct_set_field(s, fname, val)) { \
		ERROR("No such field '%s' on struct '%s'", \
				cb_strptr(cb_agent_get_string(fname)), \
				cb_strptr(cb_agent_get_string(s->spec->name))); \
	} \
	RET; \
	})

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

#undef NEXT
#undef DISPATCH
#undef ERROR
#undef READ_SIZE_T
#undef TOP
#undef FRAME
#undef LOCAL_IDX
#undef LOCAL
#undef REPLACE
#undef GLOBALS
}

#undef PUSH
#undef POP
