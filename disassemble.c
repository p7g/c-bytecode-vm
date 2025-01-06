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
					const_->val.as_module->spec));
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
			name_id = cb_modspec_name(c->val.as_module->spec);
			inner_code = c->val.as_module->code;
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
	for (size_t i = 0; i < code->bytecode_len;) {
		if (cb_disassemble_one(&code->bytecode[i], i))
			return 1;
		i += 1 + cb_opcode_arity(&code->bytecode[i]);
	}

	return 0;
}

int cb_disassemble_one(cb_instruction *bytecode, size_t offset)
{
	enum cb_opcode op;
	cb_str tmp_str;

#define NEXT() (*bytecode++)
#define NEXT_USIZE() (NEXT())

	printf("%4ld: ", offset);
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
	case OP_DUP:
	case OP_RETURN:
	case OP_NEW_STRUCT:
	case OP_ROT_2:
		printf("%s\n", cb_opcode_name(op));
		return 0;

	/* one arg */
	case OP_CONST_INT:
	case OP_CONST_DOUBLE:
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
	case OP_IMPORT_MODULE: {
		printf("%s(%zu)\n", cb_opcode_name(op), NEXT_USIZE());
		return 0;
	}

	case OP_CONST_STRING: {
		size_t string_id = NEXT_USIZE();
		cb_str string = cb_agent_get_string(string_id);
		printf("%s(\"%s\")\n", cb_opcode_name(op), cb_strptr(&string));
		return 0;
	}

	case OP_APPLY_DEFAULT_ARG: {
		size_t param_num = NEXT_USIZE(),
		       next_param_addr = NEXT_USIZE();
		printf("%s(%zu, %zu)\n", cb_opcode_name(op), param_num,
				next_param_addr);
		return 0;
	}

	case OP_LOAD_CONST:
		/* TODO: show constant value */
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

	case OP_BIND_LOCAL:
	case OP_BIND_UPVALUE: {
		size_t dest, src;
		dest = NEXT_USIZE();
		src = NEXT_USIZE();
		printf("%s(%zu, %zu)\n", cb_opcode_name(op), dest, src);
		return 0;
	}

	case OP_LOAD_FROM_MODULE: {
		size_t arg1, arg2;
		const cb_modspec *spec;
		cb_str modname, import_name;
		arg1 = NEXT_USIZE();
		arg2 = NEXT_USIZE();
		spec = cb_agent_get_modspec(arg1);
		modname = cb_agent_get_string(cb_modspec_name(spec));
		import_name = cb_agent_get_string(
				cb_modspec_get_export_name(spec, arg2));
		printf("%s(\"%s\", \"%s\")\n", cb_opcode_name(op),
				cb_strptr(&modname), cb_strptr(&import_name));
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

	case OP_MAX:
		break;
	}

	fprintf(stderr, "Unknown bytecode instruction %u\n", op);
	return -1;

#undef NEXT
#undef NEXT_USIZE
}