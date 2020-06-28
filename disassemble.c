#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "agent.h"
#include "compiler.h"
#include "disassemble.h"
#include "opcode.h"
#include "string.h"

int cb_disassemble(cb_bytecode *bytecode)
{
	size_t i, len;
	uint8_t op;

	i = 0;
	len = cb_bytecode_len(bytecode);

#define NEXT() (cb_bytecode_get(bytecode, i++))
#define NEXT_USIZE() ({ \
		size_t result = 0; \
		for (int _i = 0; _i < sizeof(size_t); _i += 1) \
			result += NEXT() << (_i * 8); \
		result; \
	})

	while (i < len) {
		printf("%4zu: ", i);
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
		case OP_RETURN:
		case OP_EXIT_MODULE:
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
		case OP_LOAD_LOCAL:
		case OP_STORE_LOCAL:
		case OP_BIND_LOCAL:
		case OP_BIND_UPVALUE:
		case OP_LOAD_UPVALUE:
		case OP_STORE_UPVALUE:
		case OP_NEW_ARRAY:
		case OP_INIT_MODULE:
		case OP_ALLOCATE_LOCALS:
		case OP_EXPORT:
		case OP_ENTER_MODULE:
			printf("%s(%zu)\n", cb_opcode_name(op), NEXT_USIZE());
			break;

		case OP_LOAD_GLOBAL:
		case OP_DECLARE_GLOBAL:
		case OP_STORE_GLOBAL:
			printf("%s(\"%s\")\n", cb_opcode_name(op),
					cb_strptr(cb_agent_get_string(
							NEXT_USIZE())));
			break;

		case OP_NEW_FUNCTION: {
			size_t arg1, arg2, arg3;
			arg1 = NEXT_USIZE();
			arg2 = NEXT_USIZE();
			arg3 = NEXT_USIZE();
			printf("%s(\"%s\", %zu, %zu)\n", cb_opcode_name(op),
					cb_strptr(cb_agent_get_string(arg1)),
					arg2, arg3);
			break;
		}

		case OP_CALL:
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
