#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "agent.h"
#include "builtin_modules.h"
#include "cbcvm.h"
#include "compiler.h"
#include "disassemble.h"
#include "eval.h"
#include "intrinsics.h"
#include "module.h"

int cb_repl(void)
{
	cb_compile_state *compile_state;
	cb_modspec *modspec;
	char *line;
	cb_bytecode *bytecode;
	int result, did_init_vm;
	size_t pc = 0;
	struct cb_frame frame;
	struct cb_module *mod;

	bytecode = cb_bytecode_new();
	modspec = cb_modspec_new(cb_agent_intern_string("<repl>", 6));
	compile_state = cb_compile_state_new("<repl>", modspec, bytecode);
	did_init_vm = 0;
	cb_agent_add_modspec(modspec);

	using_history();
	for (;;) {
		line = readline(">>> ");
		if (!line)
			break;
		if (!*line)
			continue;
		add_history(line);
		pc = cb_bytecode_len(bytecode);
		cb_compile_state_reset(compile_state, line, modspec);
		result = cb_compiler_resume(compile_state);
		free(line);
		if (did_init_vm) {
			cb_vm_state.ic = realloc(cb_vm_state.ic,
					cb_bytecode_len(bytecode)
					* sizeof(union cb_inline_cache));
			memset(&cb_vm_state.ic[pc], 0,
					cb_bytecode_len(bytecode) - pc);
		}
		if (cb_options.disasm && !result)
			result = cb_disassemble_range(bytecode, pc,
					cb_bytecode_len(bytecode));
		if (result)
			continue;
		if (!did_init_vm) {
			did_init_vm = 1;
			cb_vm_init(bytecode);
			mod = &cb_vm_state.modules[cb_modspec_id(modspec)];
			mod->spec = modspec;
			mod->global_scope = cb_hashmap_new();
			cb_instantiate_builtin_modules();
			make_intrinsics(mod->global_scope);
		}
		frame.bp = 0;
		frame.func.type = CB_VALUE_NULL;
		frame.module = NULL;
		frame.parent = NULL;
		result = cb_eval(&cb_vm_state, pc, &frame);
	}

	if (did_init_vm)
		cb_vm_deinit();
	cb_compile_state_free(compile_state);
	cb_bytecode_free(bytecode);
	return 0;
}
