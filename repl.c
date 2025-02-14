#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define HISTORY_FILE "~/.cbcvm_history"

static char *history_file_path(void)
{
	char *buf;
	char *home = getenv("HOME");

	int needed_size = snprintf(NULL, 0, "%s/.cbcvm_history", home);
	buf = malloc(sizeof(char) * (needed_size + 1));
	snprintf(buf, needed_size + 1, "%s/.cbcvm_history", home);
	buf[needed_size] = 0;

	return buf;
}

int cb_repl(void)
{
	cb_modspec *modspec;
	char *line;
	int did_init_vm;
	struct cb_code *code;
	char *history_file;

	did_init_vm = 0;
	modspec = cb_modspec_new(cb_agent_intern_string("<repl>", 6));
	cb_agent_add_modspec(modspec);

	history_file = history_file_path();
	using_history();
	read_history(history_file);
	for (;;) {
		line = readline(">>> ");
		if (!line)
			break;
		if (!*line)
			continue;
		add_history(line);
		code = cb_repl_compile(modspec, line, strlen(line));
		free(line);
		if (!code)
			continue;
		if (cb_options.disasm)
			cb_disassemble(cb_modspec_code(modspec));
		/* XXX: Why lazily init VM? */
		if (!did_init_vm) {
			did_init_vm = 1;
			cb_vm_init();
			cb_instantiate_builtin_modules();
		}
		(void) cb_run(code);
	}

	/* Clean up state */
	cb_repl_compile(NULL, NULL, 0);
	if (did_init_vm)
		cb_vm_deinit();

	stifle_history(1000);
	write_history(history_file);
	free(history_file);

	return 0;
}