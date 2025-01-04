#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "agent.h"
#include "builtin_modules.h"
#include "cbcvm.h"
#include "code.h"
#include "compiler.h"
#include "disassemble.h"
#include "eval.h"
#include "intrinsics.h"
#include "module.h"

int cb_repl(void)
{
	cb_modspec *modspec;
	char *line;
	int did_init_vm;
	struct cb_code *code;

	did_init_vm = 0;
	modspec = cb_modspec_new(cb_agent_intern_string("<repl>", 6));
	cb_agent_add_modspec(modspec);

	using_history();
	for (;;) {
		line = readline(">>> ");
		if (!line)
			break;
		if (!*line)
			continue;
		add_history(line);
		code = cb_compile_string(modspec, line);
		free(line);
		if (!code)
			continue;
		if (cb_options.disasm)
			cb_disassemble(code);
		/* XXX: Why lazily init VM? */
		if (!did_init_vm) {
			did_init_vm = 1;
			cb_vm_init();
			cb_instantiate_builtin_modules();
		}
		(void) cb_run(code);
		/* XXX: Why is this commented out? */
		// cb_code_free(code);
	}

	if (did_init_vm)
		cb_vm_deinit();
	return 0;
}