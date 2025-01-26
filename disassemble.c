#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "agent.h"
#include "code.h"
#include "compiler.h"
#include "constant.h"
#include "disassemble.h"
#include "opcode.h"
#include "str.h"
#include "value.h"

void print_const_pool(struct cb_code *code)
{
	for (int i = 0; i < code->nconsts; i += 1) {
		struct cb_const *const_ = &code->const_pool[i];
		if (const_->type == CB_CONST_MODULE) {
			cb_str name = cb_agent_get_string(cb_modspec_name(
					const_->val.as_module));
			printf("\t%d: <module %s>\n", i, cb_strptr(&name));
			continue;
		}

		struct cb_value val = cb_const_to_value(const_);
		cb_str as_str = cb_value_to_string(val);
		printf("\t%d: %s\n", i, cb_strptr(&as_str));
		cb_str_free(as_str);
	}
}

int cb_disassemble_recursive(struct cb_code *code)
{
	int result = cb_disassemble(code);
	putchar('\n');

	for (int i = 0; i < code->nconsts; i += 1) {
		struct cb_const *c = &code->const_pool[i];

		size_t name_id;
		struct cb_code *inner_code;
		if (c->type == CB_CONST_FUNCTION) {
			name_id = c->val.as_function->name;
			inner_code = c->val.as_function->code;
		} else if (c->type == CB_CONST_MODULE) {
			name_id = cb_modspec_name(c->val.as_module);
			inner_code = cb_modspec_code(c->val.as_module);
		} else {
			continue;
		}

		cb_str name = cb_agent_get_string(name_id);
		printf("%s %s\n", cb_const_type_name(c->type),
				cb_strptr(&name));
		result |= cb_disassemble_recursive(inner_code);
	}

	return result;
}

int cb_disassemble(struct cb_code *code)
{
	puts("constants:");
	print_const_pool(code);
	puts("");
	for (size_t i = 0; i < code->bytecode_len; i++) {
		if (cb_disassemble_one(code->bytecode[i], i))
			return 1;
	}

	return 0;
}

int cb_disassemble_one(cb_instruction instruction, size_t offset)
{
	union cb_op_encoding encoding;
	enum cb_opcode op;
	cb_str tmp_str;

	encoding.as_size_t = instruction;
	op = encoding.unary.op;
	size_t arg = encoding.unary.arg,
	       arg1 = encoding.binary.arg1,
	       arg2 = encoding.binary.arg2;

	printf("%4ld: ", offset);
	switch (op) {
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
	case OP_DUP:
	case OP_RETURN:
	case OP_NEW_STRUCT:
	case OP_ROT_2:
	case OP_THROW:
	case OP_POP_TRY:
	case OP_CATCH:
		printf("%s\n", cb_opcode_name(op));
		return 0;

	/* one arg */
	case OP_CONST_CHAR:
	case OP_JUMP:
	case OP_JUMP_IF_TRUE:
	case OP_JUMP_IF_FALSE:
	case OP_LOAD_LOCAL:
	case OP_STORE_LOCAL:
	case OP_LOAD_UPVALUE:
	case OP_STORE_UPVALUE:
	case OP_ALLOCATE_LOCALS:
	case OP_NEW_ARRAY_WITH_VALUES:
	case OP_CALL:
	case OP_IMPORT_MODULE:
	case OP_PUSH_TRY: {
		printf("%s(%zu)\n", cb_opcode_name(op), arg);
		return 0;
	}

	case OP_CONST_STRING: {
		size_t string_id = arg;
		cb_str string = cb_agent_get_string(string_id);
		printf("%s(\"%s\")\n", cb_opcode_name(op), cb_strptr(&string));
		return 0;
	}

	case OP_APPLY_DEFAULT_ARG: {
		size_t param_num = arg1,
		       next_param_addr = arg2;
		printf("%s(%zu, %zu)\n", cb_opcode_name(op), param_num,
				next_param_addr);
		return 0;
	}

	case OP_LOAD_CONST:
		/* TODO: show constant value */
		printf("%s(%zu)\n", cb_opcode_name(op), arg);
		return 0;

	case OP_LOAD_GLOBAL:
	case OP_DECLARE_GLOBAL:
	case OP_STORE_GLOBAL:
	case OP_LOAD_STRUCT:
	case OP_STORE_STRUCT:
	case OP_ADD_STRUCT_FIELD:
		tmp_str = cb_agent_get_string(arg);
		printf("%s(\"%s\")\n", cb_opcode_name(op), cb_strptr(&tmp_str));
		return 0;

	case OP_BIND_LOCAL:
	case OP_BIND_UPVALUE:
		printf("%s(%zu, %zu)\n", cb_opcode_name(op), arg1, arg2);
		return 0;

	case OP_LOAD_FROM_MODULE: {
		const cb_modspec *spec;
		cb_str modname, import_name;
		spec = cb_agent_get_modspec(arg1);
		modname = cb_agent_get_string(cb_modspec_name(spec));
		import_name = cb_agent_get_string(
				cb_modspec_get_export_name(spec, arg2));
		printf("%s(\"%s\", \"%s\")\n", cb_opcode_name(op),
				cb_strptr(&modname), cb_strptr(&import_name));
		return 0;
	}

	case OP_MAX:
		break;
	}

	fprintf(stderr, "Unknown bytecode instruction %u\n", op);
	return -1;

#undef NEXT
#undef NEXT_USIZE
}