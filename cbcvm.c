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
#include "string.h"
#include "value.h"

#ifdef PROFILE
# include <signal.h>

void sigint_handler(int signum)
{
	/* libc exit so gmon.out is saved */
	exit(130);
}
#endif

int main(int argc, char **argv) {
	cb_bytecode *bytecode;

	if (argc < 2) {
		fprintf(stderr, "Expected filename arg\n");
		return 1;
	}

#ifdef PROFILE
	struct sigaction sigint_action;

	sigint_action.sa_handler = sigint_handler;
	sigemptyset(&sigint_action.sa_mask);
	sigint_action.sa_flags = 0;

	sigaction(SIGINT, &sigint_action, NULL);
#endif

	cb_agent_init();

	int result = cb_compile_file(argv[1], &bytecode);
#ifdef DEBUG_DISASM
	if (!result)
		result = cb_disassemble(bytecode);
#endif
	cb_intrinsics_set_argv(argc, argv);
	if (!result)
		result = cb_eval(bytecode);

	cb_bytecode_free(bytecode);

	cb_agent_deinit();
	return result;
}
