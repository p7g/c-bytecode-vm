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
	int result;

	i = 0;
	len = cb_bytecode_len(bytecode);

	while (i < len) {
		result = cb_disassemble_one(bytecode, i);
		if (result < 0)
			return 1;
		i += result;
	}

	return 0;
}

int cb_disassemble_one(cb_bytecode *bytecode, size_t pc)
{
	cb_instruction op;
	size_t len = cb_bytecode_len(bytecode);
	size_t offset = 0;

#define NEXT() ({ \
		if (pc + offset >= len) { \
			fprintf(stderr, "Unexpected end of bytecode\n"); \
			return -1; \
		} \
		cb_bytecode_get(bytecode, pc + offset++); \
	})
#define NEXT_USIZE() ({ \
		size_t result = 0; \
		for (int _i = 0; _i < sizeof(size_t) / sizeof(cb_instruction); _i += 1) \
			result += NEXT() << (_i * 8 * sizeof(cb_instruction)); \
		result; \
	})
#define WITH_ARGS(NARGS) (1 + sizeof(size_t) / sizeof(cb_instruction) * (NARGS))

	printf("%4zu: ", pc + offset);
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
	case OP_NEG:
	case OP_END_MODULE:
	case OP_DUP:
	case OP_EXIT_MODULE:
	case OP_PREP_FOR_CALL:
		printf("%s\n", cb_opcode_name(op));
		return WITH_ARGS(0);

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
	case OP_INIT_MODULE:
	case OP_ALLOCATE_LOCALS:
	case OP_EXPORT:
	case OP_ENTER_MODULE:
	case OP_NEW_ARRAY_WITH_VALUES:
	case OP_RETURN:
	case OP_CALL:
		printf("%s(%zu)\n", cb_opcode_name(op), NEXT_USIZE());
		return WITH_ARGS(1);

	case OP_LOAD_GLOBAL:
	case OP_DECLARE_GLOBAL:
	case OP_STORE_GLOBAL:
		printf("%s(\"%s\")\n", cb_opcode_name(op),
				cb_strptr(cb_agent_get_string(
						NEXT_USIZE())));
		return WITH_ARGS(1);

	case OP_NEW_FUNCTION: {
		size_t arg1, arg2, arg3;
		arg1 = NEXT_USIZE();
		arg2 = NEXT_USIZE();
		arg3 = NEXT_USIZE();
		printf("%s(\"%s\", %zu, %zu)\n", cb_opcode_name(op),
				(arg1 == (size_t) -1 - 1)
				? "<anonymous>"
				: cb_strptr(cb_agent_get_string(arg1)),
				arg2, arg3);
		return WITH_ARGS(3);
	}

	case OP_LOAD_FROM_MODULE: {
		size_t arg1, arg2;
		arg1 = NEXT_USIZE();
		arg2 = NEXT_USIZE();
		printf("%s(%zu, %zu)\n", cb_opcode_name(op), arg1,
				arg2);
		return WITH_ARGS(2);
	}

	default:
		fprintf(stderr, "Unknown bytecode instruction %d\n", op);
		return -1;
	}

#undef NEXT
#undef NEXT_USIZE
#undef WITH_ARGS
}
