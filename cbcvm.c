#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "compiler.h"
#include "disassemble.h"
#include "eval.h"
#include "gc.h"
#include "intrinsics.h"
#include "str.h"
#include "repl.h"
#include "value.h"

#ifdef PROFILE
# include <signal.h>

void sigint_handler(int signum)
{
	/* libc exit so gmon.out is saved */
	exit(130);
}
#endif

#ifdef DEBUG_DISASM
static const int disasm = 1;
#else
static const int disasm = 0;
#endif

int run_repl(void)
{
	return cb_repl(disasm);
}

int run_file(const char *filename)
{
	cb_bytecode *bytecode;
	int result;

	result = cb_compile_file("<script>", filename, &bytecode);

	if (disasm && !result)
		result = cb_disassemble(bytecode);

	if (!result) {
		cb_vm_init(bytecode);
		result = cb_run();
		cb_vm_deinit();
	}

	cb_bytecode_free(bytecode);

	return result;
}

int main(int argc, char **argv) {
	int result;

#ifdef PROFILE
	struct sigaction sigint_action;

	sigint_action.sa_handler = sigint_handler;
	sigemptyset(&sigint_action.sa_mask);
	sigint_action.sa_flags = 0;

	sigaction(SIGINT, &sigint_action, NULL);
#endif

	if (cb_agent_init())
		return 1;

	cb_intrinsics_set_argv(argc, argv);
	if (argc < 2) {
		result = run_repl();
	} else {
		result = run_file(argv[1]);
	}

	cb_agent_deinit();
	return result;
}
