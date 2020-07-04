#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "compiler.h"
#include "eval.h"
#include "hashmap.h"
#include "intrinsics.h"
#include "opcode.h"
#include "value.h"
#include "string.h"

#define STACK_MAX 30000
#define STACK_INIT_SIZE 1024

struct upvalue {
	int is_open;
	union {
		size_t idx;
		struct cb_value value;
	} v;
};

struct frame {
	size_t prev_pc,
	       prev_bp,
	       module_id,
	       current_function;
};

/* global vm state... this is fine, right?
 * These values are also used by the GC */
size_t sp;
struct cb_value *stack;
struct upvalue *upvalues;
size_t upvalues_idx;
/* size of this array is based on number of modspecs in agent */
struct cb_module *modules;
cb_hashmap *globals;

int cb_eval(cb_bytecode *bytecode)
{
	size_t pc, bp;
	size_t stack_size, upvalues_size;
	struct frame *call_stack;
	size_t call_stack_idx, call_stack_size;
	struct cb_module *current_module;

	pc = 0;
	stack = malloc(STACK_INIT_SIZE * sizeof(struct cb_value));
	sp = 0;
	bp = 0;
	stack_size = STACK_INIT_SIZE;
	upvalues = calloc(32, sizeof(struct upvalue));
	upvalues_idx = 0;
	upvalues_size = 32;
	call_stack = malloc(sizeof(struct frame) * 256);
	call_stack_idx = 0;
	call_stack_size = 256;
	modules = calloc(cb_agent_modspec_count(), sizeof(struct cb_module));
	current_module = NULL;
	globals = cb_hashmap_new();
	make_intrinsics(globals);

#define TABLE_ENTRY(OP) &&DO_##OP,
	static void *dispatch_table[] = {
		CB_OPCODE_LIST(TABLE_ENTRY)
	};
#undef TABLE_ENTRY

#define NEXT() (cb_bytecode_get(bytecode, pc++))
#ifdef DEBUG_PC
# define DISPATCH() ({ \
		printf("pc: %zu\n", pc); \
		size_t _next = NEXT(); \
		assert(_next >= 0 && _next < OP_MAX); \
		goto *dispatch_table[_next]; \
	})
#else
# define DISPATCH() ({ \
		size_t _next = NEXT(); \
		assert(_next >= 0 && _next < OP_MAX); \
		goto *dispatch_table[_next]; \
	})
#endif

#define ERROR(MSG, ...) ({ \
		fprintf(stderr, (MSG), ##__VA_ARGS__); \
		return 1; \
	})
#define PUSH(V) ({ \
		struct cb_value _v = (V); \
		if (sp >= stack_size) { \
			stack_size <<= 1; \
			stack = realloc(stack, stack_size \
					* sizeof(struct cb_value)); \
		} \
		stack[sp++] = _v; \
	})
#define READ_SIZE_T() ({ \
		int _i = 0; \
		size_t _val = 0; \
		for (_i = 0; _i < sizeof(size_t); _i += 1) \
			_val += ((size_t) NEXT()) << (_i * 8); \
		_val; \
	})
#define POP() ({ \
		assert(sp > 0); \
		struct cb_value _v = stack[--sp]; \
		_v; \
	})
#define TOP() (stack[sp - 1])
#define FRAME() (&call_stack[call_stack_idx - 1])
#define LOCAL_IDX(N) (bp + 1 + (N))
#define LOCAL(N) ({ \
		struct cb_value _v = stack[LOCAL_IDX(N)]; \
		_v; \
	})
#define REPLACE(N, VAL) ({ \
		struct cb_value _v = (VAL); \
		stack[(N)] = _v; \
	})
#define GLOBALS() (current_module ? current_module->global_scope : globals)

	DISPATCH();

DO_OP_MAX:
DO_OP_HALT:
	free(modules);
	return 0;

DO_OP_CONST_INT: {
	size_t as_size_t = READ_SIZE_T();
	struct cb_value val;
	val.type = CB_VALUE_INT;
	val.val.as_int = (intptr_t) as_size_t;
	PUSH(val);
	DISPATCH();

DO_OP_CONST_DOUBLE: {
	struct cb_value val;
	val.type = CB_VALUE_DOUBLE;
	val.val.as_double = (double) READ_SIZE_T();
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

/* FIXME: Shouldn't allocate these each time */
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
			return 1; \
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
	if (a.type != CB_VALUE_INT || b.type != CB_VALUE_INT) {
		ERROR("Operands to '%%' must be integers\n");
		return 1;
	}
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
	if (cb_value_is_truthy(&pred))
		pc = next;
	DISPATCH();
}

DO_OP_JUMP_IF_FALSE: {
	struct cb_value pred;
	size_t next = READ_SIZE_T();
	pred = POP();
	if (!cb_value_is_truthy(&pred))
		pc = next;
	DISPATCH();
}

DO_OP_CALL: {
	size_t num_args, name;
	struct cb_value func_val, result;
	struct cb_function *func;
	struct frame frame;

	num_args = READ_SIZE_T();
	func_val = stack[sp - num_args - 1];

	if (func_val.type != CB_VALUE_FUNCTION) {
		ERROR("Value of type '%s' is not callable\n",
				cb_value_type_name(func_val.type));
		return 1;
	}

	func = func_val.val.as_function;
	if (func->type == CB_FUNCTION_NATIVE) {
		if (func->value.as_native(num_args, &stack[sp - num_args],
					&result))
			return 1;
		assert(sp > num_args);
		sp -= (num_args + 1);
		PUSH(result);
	} else {
		name = func->name;
		if (func->arity > num_args) {
			ERROR("Too few arguments to function '%s'\n",
					func->name == (size_t) -1
					? "<anonymous>"
					: cb_strptr(cb_agent_get_string(name)));
			return 1;
		}
		frame.prev_bp = bp;
		frame.prev_pc = pc;
		frame.current_function = call_stack_idx == 0 ? -1 : bp;
		if (call_stack_idx >= call_stack_size) {
			call_stack_size <<= 1;
			call_stack = realloc(call_stack, call_stack_size
					* sizeof(struct frame));
		}
		call_stack[call_stack_idx++] = frame;
		/* jump in */
		bp = sp - num_args - 1;
		pc = func->value.as_user;
	}

	DISPATCH();
}

DO_OP_RETURN: {
	struct cb_value retval;
	struct frame frame;

	retval = POP();
	/* FIXME: bounds check */
	frame = call_stack[--call_stack_idx];

	/* TODO: close upvalues */
	sp = bp;
	bp = frame.prev_bp;
	pc = frame.prev_pc;
	PUSH(retval);

	DISPATCH();
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

	if (value == NULL) {
		ERROR("Unbound global '%s'\n",
				cb_strptr(cb_agent_get_string(id)));
		return 1;
	}

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
	size_t name, arity, address;
	struct cb_function *func;
	struct cb_value func_val;

	name = READ_SIZE_T();
	arity = READ_SIZE_T();
	address = READ_SIZE_T();

	func = cb_function_new();
	func->type = CB_FUNCTION_USER;
	func->name = name;
	func->arity = arity;
	func->value.as_user = address;

	func_val.type = CB_VALUE_FUNCTION;
	func_val.val.as_function = func;

	PUSH(func_val);
	DISPATCH();
}

DO_OP_BIND_LOCAL:
DO_OP_BIND_UPVALUE:
DO_OP_LOAD_UPVALUE:
DO_OP_STORE_UPVALUE:
	return 1;

DO_OP_LOAD_FROM_MODULE: {
	size_t mod_id, export_id, export_name;
	struct cb_module *mod;
	struct cb_value *val;
	
	mod_id = READ_SIZE_T();
	export_id = READ_SIZE_T();
	mod = &modules[mod_id];
	export_name = cb_modspec_get_export_name(mod->spec, export_id);
	val = cb_hashmap_get(mod->global_scope, export_name);
	assert(val != NULL);
	PUSH(*val);
	DISPATCH();
}

DO_OP_EXPORT:
	/* FIXME: use export IDs rather than hashmap lookups */
	READ_SIZE_T();
	DISPATCH();

DO_OP_NEW_ARRAY:
DO_OP_NEW_ARRAY_WITH_VALUES:
DO_OP_ARRAY_GET:
DO_OP_ARRAY_SET:
	return 1;

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
		int _result = cb_value_cmp(&(A), &(B), &_ok); \
		if (!_ok) { \
			ERROR("Cannot compare values of types %s and %s\n", \
					cb_value_type_name((A).type), \
					cb_value_type_name((B).type)); \
			return 1; \
		} \
		_result; \
	})

DO_OP_LESS_THAN: {
	int diff;
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
	int diff;
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
	int diff;
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
	int diff;
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

DO_OP_BITWISE_AND:
DO_OP_BITWISE_OR:
DO_OP_BITWISE_XOR:
DO_OP_BITWISE_NOT:
DO_OP_NOT:
DO_OP_NEG:
	return 1;

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
	modules[module_id] = mod;
	current_module = &modules[module_id];

	DISPATCH();
}

DO_OP_ENTER_MODULE:
	current_module = &modules[READ_SIZE_T()];
	DISPATCH();


DO_OP_END_MODULE:
DO_OP_EXIT_MODULE:
	current_module = NULL;
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
}

#undef NEXT
#undef DISPATCH
#undef PUSH
#undef ERROR
#undef READ_SIZE_T
#undef POP
#undef TOP
#undef FRAME
#undef LOCAL_IDX
#undef LOCAL
#undef REPLACE
#undef GLOBALS
}
