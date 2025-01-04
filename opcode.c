#include <assert.h>

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

size_t cb_opcode_arity(const cb_instruction *ops)
{
	enum cb_opcode op;

	assert(*ops >= 0 && *ops < OP_MAX);
	op = *ops;

	switch (op) {
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
	case OP_DUP:
	case OP_RETURN:
	case OP_NEW_STRUCT:
	case OP_ROT_2:
	case OP_MAX:
		return 0;

	case OP_LOAD_CONST:
	case OP_CONST_INT:
	case OP_CONST_DOUBLE:
	case OP_CONST_STRING:
	case OP_CONST_CHAR:
	case OP_JUMP:
	case OP_JUMP_IF_TRUE:
	case OP_JUMP_IF_FALSE:
	case OP_LOAD_LOCAL:
	case OP_STORE_LOCAL:
	case OP_LOAD_UPVALUE:
	case OP_STORE_UPVALUE:
	case OP_ALLOCATE_LOCALS:
	case OP_EXPORT:
	case OP_NEW_ARRAY_WITH_VALUES:
	case OP_CALL:
	case OP_LOAD_GLOBAL:
	case OP_DECLARE_GLOBAL:
	case OP_STORE_GLOBAL:
	case OP_LOAD_STRUCT:
	case OP_STORE_STRUCT:
	case OP_ADD_STRUCT_FIELD:
	case OP_IMPORT_MODULE:
		return 1;

	case OP_BIND_LOCAL:
	case OP_BIND_UPVALUE:
	case OP_LOAD_FROM_MODULE:
	case OP_APPLY_DEFAULT_ARG:
		return 2;

	case OP_NEW_STRUCT_SPEC:
		return 2 + ops[2];
	}
}

int cb_opcode_stack_effect(const cb_instruction *ops)
{
	enum cb_opcode op;

	assert(*ops >= 0 && *ops < OP_MAX);
	op = *ops;

	switch (op) {
	case OP_LOAD_CONST:
	case OP_CONST_TRUE:
	case OP_CONST_FALSE:
	case OP_CONST_NULL:
	case OP_CONST_INT:
	case OP_CONST_DOUBLE:
	case OP_CONST_STRING:
	case OP_CONST_CHAR:
	case OP_JUMP:
	case OP_DUP:
	case OP_LOAD_LOCAL:
	case OP_LOAD_UPVALUE:
	case OP_LOAD_GLOBAL:
	case OP_LOAD_STRUCT:
	case OP_LOAD_FROM_MODULE:
	case OP_NEW_STRUCT_SPEC:
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
	case OP_BITWISE_NOT:
	case OP_RETURN:
	case OP_JUMP_IF_TRUE:
	case OP_JUMP_IF_FALSE:
	case OP_EXPORT:
	case OP_STORE_STRUCT:
	case OP_ADD_STRUCT_FIELD:
		return -1;

	case OP_ARRAY_SET:
		return -2;

	case OP_HALT:
	case OP_NOT:
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
		return 0;

	case OP_ALLOCATE_LOCALS:
		return ops[1];

	case OP_NEW_ARRAY_WITH_VALUES:
		return -ops[1];

	case OP_CALL:
		return -ops[1];
	}
}