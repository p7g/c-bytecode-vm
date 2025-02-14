#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "opcode.h"
#include "compiler.h"

const char *cb_opcode_name(enum cb_opcode op)
{
#define CASE(O) case O: return #O;
	switch (op) {
	CB_OPCODE_LIST(CASE)
	default:
		return "";
	}
#undef CASE
}

int cb_opcode_stack_effect(const cb_instruction instruction)
{
	union cb_op_encoding op;
	op.as_size_t = instruction;

	assert(op.unary.op < OP_MAX);

	switch (op.unary.op) {
	case OP_LOAD_CONST:
	case OP_CONST_TRUE:
	case OP_CONST_FALSE:
	case OP_CONST_NULL:
	case OP_CONST_STRING:
	case OP_JUMP:
	case OP_DUP:
	case OP_LOAD_LOCAL:
	case OP_LOAD_UPVALUE:
	case OP_LOAD_GLOBAL:
	case OP_LOAD_STRUCT:
	case OP_LOAD_FROM_MODULE:
	case OP_CATCH:
		return 1;

	case OP_ADD:
	case OP_SUB:
	case OP_MUL:
	case OP_DIV:
	case OP_MOD:
	case OP_EXP:
	case OP_POP:
	case OP_ARRAY_GET:
	case OP_EQUAL:
	case OP_NOT_EQUAL:
	case OP_LESS_THAN:
	case OP_LESS_THAN_EQUAL:
	case OP_GREATER_THAN:
	case OP_GREATER_THAN_EQUAL:
	case OP_BITWISE_OR:
	case OP_BITWISE_AND:
	case OP_BITWISE_XOR:
	case OP_RETURN:
	case OP_JUMP_IF_TRUE:
	case OP_JUMP_IF_FALSE:
	case OP_STORE_STRUCT:
	case OP_ADD_STRUCT_FIELD:
	case OP_THROW:
		return -1;

	case OP_ARRAY_SET:
		return -2;

	case OP_HALT:
	case OP_NOT:
	case OP_BITWISE_NOT:
	case OP_NEG:
	case OP_NEW_STRUCT:
	case OP_ROT_2:
	case OP_STORE_LOCAL:
	case OP_MAX:
	case OP_BIND_LOCAL:
	case OP_BIND_UPVALUE:
	case OP_STORE_UPVALUE:
	case OP_DECLARE_GLOBAL:
	case OP_STORE_GLOBAL:
	case OP_APPLY_DEFAULT_ARG:
	case OP_IMPORT_MODULE:
	case OP_PUSH_TRY:
	case OP_POP_TRY:
		return 0;

	case OP_ALLOCATE_LOCALS:
		return op.unary.arg;

	case OP_NEW_ARRAY_WITH_VALUES:
		return -op.unary.arg + 1;

	case OP_CALL:
		return -op.unary.arg;

	default:
		fprintf(stderr, "getting stack effect of unknown opcode %d\n",
				op.unary.op);
		abort();
	}
}

enum cb_opcode cb_opcode_assert(size_t n)
{
	switch (n) {
#define CASE(OP) case OP: return OP;
	CB_OPCODE_LIST(CASE)
#undef CASE
	default:
		fprintf(stderr, "Invalid opcode: %zu\n", n);
		abort();
	}
}