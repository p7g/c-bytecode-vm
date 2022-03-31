#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "opcode.h"

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

int cb_opcode_stack_effect(enum cb_opcode op, cb_instruction *args)
{
	switch (op) {
	case OP_HALT:
	case OP_JUMP:
	case OP_STORE_LOCAL:
	case OP_DECLARE_GLOBAL:
	case OP_STORE_GLOBAL:
	case OP_BIND_LOCAL:
	case OP_BIND_UPVALUE:
	case OP_STORE_UPVALUE:
	case OP_BITWISE_NOT:
	case OP_NOT:
	case OP_NEG:
	case OP_INIT_MODULE:
	case OP_END_MODULE:
	case OP_ENTER_MODULE:
	case OP_EXIT_MODULE:
	case OP_NEW_STRUCT:
	case OP_LOAD_STRUCT:
	case OP_ROT_2:
	case OP_MAX:
		return 0;
	case OP_CONST_INT:
	case OP_CONST_DOUBLE:
	case OP_CONST_STRING:
	case OP_CONST_CHAR:
	case OP_CONST_TRUE:
	case OP_CONST_FALSE:
	case OP_CONST_NULL:
	case OP_LOAD_LOCAL:
	case OP_LOAD_GLOBAL:
	case OP_NEW_FUNCTION:
	case OP_LOAD_UPVALUE:
	case OP_LOAD_FROM_MODULE:
	case OP_DUP:
	case OP_NEW_STRUCT_SPEC:
		return 1;
	case OP_CALL:
		return -args[0];
	case OP_NEW_ARRAY_WITH_VALUES:
		return 1 - args[0];
	case OP_ADD:
	case OP_SUB:
	case OP_MUL:
	case OP_DIV:
	case OP_MOD:
	case OP_EXP:
	case OP_JUMP_IF_TRUE:
	case OP_JUMP_IF_FALSE:
	case OP_RETURN:
	case OP_POP:
	case OP_ARRAY_GET:
	case OP_EQUAL:
	case OP_NOT_EQUAL:
	case OP_LESS_THAN:
	case OP_LESS_THAN_EQUAL:
	case OP_GREATER_THAN:
	case OP_GREATER_THAN_EQUAL:
	case OP_BITWISE_AND:
	case OP_BITWISE_OR:
	case OP_BITWISE_XOR:
	case OP_STORE_STRUCT:
	case OP_ADD_STRUCT_FIELD:
		return -1;
	case OP_ARRAY_SET:
		return -2;
	default:
		fprintf(stderr, "Unknown opcode: %d\n", op);
		abort();
	}
}

unsigned cb_opcode_nargs(enum cb_opcode op, cb_instruction *args)
{
	switch (op) {
	case OP_MAX:
	case OP_HALT:
	case OP_CONST_TRUE:
	case OP_CONST_FALSE:
	case OP_CONST_NULL:
	case OP_ADD:
	case OP_SUB:
	case OP_MUL:
	case OP_DIV:
	case OP_MOD:
	case OP_EXP:
	case OP_POP:
	case OP_ARRAY_GET:
	case OP_ARRAY_SET:
	case OP_EQUAL:
	case OP_NOT_EQUAL:
	case OP_LESS_THAN:
	case OP_LESS_THAN_EQUAL:
	case OP_GREATER_THAN:
	case OP_GREATER_THAN_EQUAL:
	case OP_BITWISE_OR:
	case OP_BITWISE_AND:
	case OP_BITWISE_XOR:
	case OP_BITWISE_NOT:
	case OP_NOT:
	case OP_NEG:
	case OP_END_MODULE:
	case OP_DUP:
	case OP_RETURN:
	case OP_EXIT_MODULE:
	case OP_NEW_STRUCT:
	case OP_ROT_2:
		return 0;

	case OP_CONST_INT:
	case OP_CONST_DOUBLE:
	case OP_CONST_STRING:
	case OP_CONST_CHAR:
	case OP_JUMP:
	case OP_JUMP_IF_TRUE:
	case OP_JUMP_IF_FALSE:
	case OP_LOAD_LOCAL:
	case OP_STORE_LOCAL:
	case OP_BIND_LOCAL:
	case OP_BIND_UPVALUE:
	case OP_LOAD_UPVALUE:
	case OP_STORE_UPVALUE:
	case OP_INIT_MODULE:
	case OP_ENTER_MODULE:
	case OP_NEW_ARRAY_WITH_VALUES:
	case OP_CALL:
	case OP_LOAD_GLOBAL:
	case OP_DECLARE_GLOBAL:
	case OP_STORE_GLOBAL:
	case OP_LOAD_STRUCT:
	case OP_STORE_STRUCT:
	case OP_ADD_STRUCT_FIELD:
		return 1;

	case OP_NEW_FUNCTION:
		return 6 + args[5];

	case OP_LOAD_FROM_MODULE:
		return 2;

	case OP_NEW_STRUCT_SPEC:
		return 2 + args[1];

	default:
		fprintf(stderr, "Unknown opcode: %d\n", op);
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
