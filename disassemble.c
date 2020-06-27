#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "disassemble.h"
#include "opcode.h"

int cb_disassemble(uint8_t *bytecode, size_t len)
{
	size_t i;
	uint8_t op;

	i = 0;

#define NEXT() (bytecode[i++])
#define NEXT_USIZE() ({ \
		size_t result = NEXT(); \
		result += ((size_t) NEXT()) << 8; \
		result += ((size_t) NEXT()) << 16; \
		result += ((size_t) NEXT()) << 24; \
		result += ((size_t) NEXT()) << 32; \
		result += ((size_t) NEXT()) << 40; \
		result += ((size_t) NEXT()) << 48; \
		result += ((size_t) NEXT()) << 56; \
		result; \
	})

	while (i < len) {
		switch ((op = NEXT())) {
		/* no args */
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
		case OP_EXPORT:
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
		case OP_LEFT_SHIFT:
		case OP_RIGHT_SHIFT:
		case OP_NEG:
		case OP_END_MODULE:
		case OP_DUP:
			printf("%s\n", cb_opcode_name(op));
			break;

		/* one arg */
		case OP_CONST_INT:
		case OP_CONST_DOUBLE:
		case OP_CONST_STRING:
		case OP_CONST_CHAR:
		case OP_JUMP:
		case OP_JUMP_IF_TRUE:
		case OP_JUMP_IF_FALSE:
		case OP_RETURN:
		case OP_LOAD_LOCAL:
		case OP_STORE_LOCAL:
		case OP_LOAD_GLOBAL:
		case OP_DECLARE_GLOBAL:
		case OP_STORE_GLOBAL:
		case OP_BIND_LOCAL:
		case OP_BIND_UPVALUE:
		case OP_LOAD_UPVALUE:
		case OP_STORE_UPVALUE:
		case OP_NEW_ARRAY:
		case OP_INIT_MODULE:
		case OP_ALLOCATE_LOCALS:
			printf("%s(%zu)\n", cb_opcode_name(op), NEXT_USIZE());
			break;

		case OP_CALL:
		case OP_NEW_FUNCTION:
		case OP_LOAD_FROM_MODULE:
		case OP_NEW_ARRAY_WITH_VALUES:
		default:
			fprintf(stderr, "Unknown bytecode instruction\n");
			return 1;
		}
	}

#undef NEXT

	return 0;
}
