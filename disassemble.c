#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "agent.h"
#include "compiler.h"
#include "disassemble.h"
#include "opcode.h"
#include "str.h"

int cb_disassemble(cb_bytecode *bytecode)
{
	return cb_disassemble_range(bytecode, 0, cb_bytecode_len(bytecode));
}

int cb_disassemble_range(cb_bytecode *bytecode, size_t start, size_t end)
{
	size_t i;

	assert(start < end);
	assert(end <= cb_bytecode_len(bytecode));

	i = start;

	while (i < end) {
		if (cb_disassemble_one(bytecode, i))
			return 1;
		i += 1 + cb_opcode_nargs(
				cb_opcode_assert(cb_bytecode_get(bytecode, i)),
				bytecode->code + i + 1);
	}

	return 0;
}

int cb_disassemble_one(cb_bytecode *bytecode, size_t pc)
{
	cb_instruction op;
	size_t len = cb_bytecode_len(bytecode);
	size_t offset = 0;
	cb_str tmp_str;

#define NEXT() ({ \
		if (pc + offset >= len) { \
			fprintf(stderr, "Unexpected end of bytecode\n"); \
			return -1; \
		} \
		cb_bytecode_get(bytecode, pc + offset++); \
	})
#define NEXT_USIZE() (NEXT())

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
	case OP_RETURN:
	case OP_EXIT_MODULE:
	case OP_NEW_STRUCT:
	case OP_ROT_2:
		printf("%s\n", cb_opcode_name(op));
		return 0;

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
	case OP_ENTER_MODULE:
	case OP_NEW_ARRAY_WITH_VALUES:
	case OP_CALL:
		printf("%s(%zu)\n", cb_opcode_name(op), NEXT_USIZE());
		return 0;

	case OP_LOAD_GLOBAL:
	case OP_DECLARE_GLOBAL:
	case OP_STORE_GLOBAL:
	case OP_LOAD_STRUCT:
	case OP_STORE_STRUCT:
	case OP_ADD_STRUCT_FIELD:
		tmp_str = cb_agent_get_string(NEXT_USIZE());
		printf("%s(\"%s\")\n", cb_opcode_name(op), cb_strptr(&tmp_str));
		return 0;

	case OP_NEW_FUNCTION: {
		size_t arg1, arg2, arg3, nopt, tmp;
		arg1 = NEXT_USIZE();
		arg2 = NEXT_USIZE();
		arg3 = NEXT_USIZE();
		nopt = NEXT_USIZE();
		tmp_str = cb_agent_get_string(arg1);
		printf("%s(\"%s\", %zu, %zu, %zu", cb_opcode_name(op),
				cb_strptr(&tmp_str), arg2, arg3, nopt);
		if (nopt > 0) {
			tmp = nopt;
			while (tmp--)
				printf(", %zu", NEXT_USIZE());
		}
		fputs(")\n", stdout);
		return 0;
	}

	case OP_LOAD_FROM_MODULE: {
		size_t arg1, arg2;
		arg1 = NEXT_USIZE();
		arg2 = NEXT_USIZE();
		printf("%s(%zu, %zu)\n", cb_opcode_name(op), arg1,
				arg2);
		return 0;
	}

	case OP_NEW_STRUCT_SPEC: {
		size_t name_id, nfields, field_name, i;
		name_id = NEXT_USIZE();
		nfields = NEXT_USIZE();
		tmp_str = cb_agent_get_string(name_id);
		printf("%s(\"%s\"", cb_opcode_name(op), cb_strptr(&tmp_str));
		for (i = 0; i < nfields; i += 1) {
			field_name = NEXT_USIZE();
			tmp_str = cb_agent_get_string(field_name);
			printf(", \"%s\"", cb_strptr(&tmp_str));
		}
		puts(")");
		return 0;
	}

	default:
		fprintf(stderr, "Unknown bytecode instruction %zu\n", op);
		return -1;
	}

#undef NEXT
#undef NEXT_USIZE
}
