#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "agent.h"
#include "cbcvm.h"
#include "compiler.h"
#include "disassemble.h"
#include "eval.h"
#include "module.h"

int cb_repl(void)
{
	cb_modspec *modspec;
	char *line;
	cb_bytecode *bytecode;
	int result, did_init_vm;
	size_t pc = 0;
	struct cb_frame frame;

	modspec = NULL;
	bytecode = cb_bytecode_new();
	did_init_vm = 0;

	using_history();
	for (;;) {
		line = readline(">>> ");
		if (!line)
			break;
		if (!*line)
			continue;
		add_history(line);
		pc = cb_bytecode_len(bytecode);
		modspec = cb_compile_string(bytecode, "<repl>", line, modspec);
		if (cb_options.disasm && modspec)
			result = cb_disassemble_range(bytecode, pc,
					cb_bytecode_len(bytecode));
		if (result)
			continue;
		if (!did_init_vm) {
			did_init_vm = 1;
			cb_vm_init(bytecode);
		}
		frame.bp = 0;
		frame.is_function = 0;
		frame.module = NULL;
		frame.parent = NULL;
		result = cb_eval(pc, &frame);
		free(line);
	}

	if (did_init_vm)
		cb_vm_deinit();

	return 0;
}
