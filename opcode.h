#ifndef cb_opcode_h
#define cb_opcode_h

#include "compiler.h"

#define CB_OPCODE_LIST(X) \
	X(OP_HALT) \
	X(OP_CONST_INT) \
	X(OP_CONST_DOUBLE) \
	X(OP_CONST_STRING) \
	X(OP_CONST_CHAR) \
	X(OP_CONST_TRUE) \
	X(OP_CONST_FALSE) \
	X(OP_CONST_NULL) \
	X(OP_ADD) \
	X(OP_SUB) \
	X(OP_MUL) \
	X(OP_DIV) \
	X(OP_MOD) \
	X(OP_EXP) \
	X(OP_JUMP) \
	X(OP_JUMP_IF_TRUE) \
	X(OP_JUMP_IF_FALSE) \
	X(OP_CALL) \
	X(OP_RETURN) \
	X(OP_POP) \
	X(OP_LOAD_LOCAL) \
	X(OP_STORE_LOCAL) \
	X(OP_LOAD_GLOBAL) \
	X(OP_DECLARE_GLOBAL) \
	X(OP_STORE_GLOBAL) \
	X(OP_NEW_FUNCTION) \
	X(OP_BIND_LOCAL) \
	X(OP_BIND_UPVALUE) \
	X(OP_LOAD_UPVALUE) \
	X(OP_STORE_UPVALUE) \
	X(OP_LOAD_FROM_MODULE) \
	X(OP_NEW_ARRAY_WITH_VALUES) \
	X(OP_ARRAY_GET) \
	X(OP_ARRAY_SET) \
	X(OP_EQUAL) \
	X(OP_NOT_EQUAL) \
	X(OP_LESS_THAN) \
	X(OP_LESS_THAN_EQUAL) \
	X(OP_GREATER_THAN) \
	X(OP_GREATER_THAN_EQUAL) \
	X(OP_BITWISE_AND) \
	X(OP_BITWISE_OR) \
	X(OP_BITWISE_XOR) \
	X(OP_BITWISE_NOT) \
	X(OP_NOT) \
	X(OP_NEG) \
	X(OP_INIT_MODULE) \
	X(OP_END_MODULE) \
	X(OP_DUP) \
	X(OP_ENTER_MODULE) \
	X(OP_EXIT_MODULE) \
	X(OP_NEW_STRUCT) \
	X(OP_LOAD_STRUCT) \
	X(OP_STORE_STRUCT) \
	X(OP_ADD_STRUCT_FIELD) \
	X(OP_NEW_STRUCT_SPEC) \
	X(OP_ROT_2) \
	X(OP_MAX)

enum cb_opcode {
#define COMMA(V) V,
	CB_OPCODE_LIST(COMMA)
#undef COMMA
};

const char *cb_opcode_name(enum cb_opcode);
int cb_opcode_stack_effect(enum cb_opcode, cb_instruction *);
unsigned cb_opcode_nargs(enum cb_opcode, cb_instruction *);
enum cb_opcode cb_opcode_assert(size_t);

#endif
