#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "compiler.h"
#include "disassemble.h"
#include "gc.h"
#include "string.h"
#include "value.h"

int main(int argc, char **argv) {
	size_t bytecode_len;
	uint8_t *bytecode;

	if (argc < 2) {
		fprintf(stderr, "Expected filename arg\n");
		return 1;
	}

	cb_agent_init();

	int result = cb_compile_file(argv[1], &bytecode, &bytecode_len);
	if (!result) {
		result = cb_disassemble(bytecode, bytecode_len);
	}

	free(bytecode);

	cb_agent_deinit();
	return result;
}
