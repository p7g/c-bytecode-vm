#include <getopt.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "builtin_modules.h"
#include "cbcvm.h"
#include "compiler.h"
#include "disassemble.h"
#include "eval.h"
#include "gc.h"
#include "intrinsics.h"
#include "str.h"
#include "repl.h"
#include "value.h"

struct cb_options cb_options;

#ifdef PROFILE
# include <signal.h>

void sigint_handler(int signum)
{
	/* libc exit so gmon.out is saved */
	exit(130);
}
#endif

int run_file(const char *filename)
{
	struct cb_code *code;
	cb_modspec *module;
	int result;
	FILE *f;

	f = fopen(filename, "rb");
	if (!f) {
		perror("fopen");
		return 1;
	}

	module = cb_modspec_new(cb_agent_intern_string("<main>", 6));
	cb_agent_add_modspec(module);
	code = cb_compile_file(module, f);
	cb_gc_hold_key *code_hold = cb_code_gc_hold(code);

	if (!code)
		return 1;

	if (cb_options.disasm) {
		if (cb_disassemble_recursive(code)) {
			return 1;
		}
	}

	cb_vm_init();
	cb_instantiate_builtin_modules();
	cb_gc_enable();
	result = cb_run(code);
	cb_vm_deinit();
	cb_gc_release(code_hold);

	return result;
}

static const char *help = (
	"Usage:\n"
	"\tcbcvm [options]\n"
	"\tcbcvm [options] <file> [<arg>]...\n"
	"\tcbcvm -h | --help\n"
	"\n"
	"Options:\n"
	"\t-h, --help      Print this help text.\n"
	"\t-D              Enable all \"debug-\" features.\n"
	"\t--debug-gc      Print GC debug information during execution.\n"
	"\t--debug-disasm  Print a disassembly of the program before execution.\n"
	"\t--stress-gc     Collect garbage after every allocation.\n"
);

static int parse_opts(int *argc, char ***argv, char **fname_out)
{
	static struct option long_opts[] = {
		{"debug-gc",        no_argument, &cb_options.debug_gc,      1  },
		{"debug-hashmap",   no_argument, &cb_options.debug_hashmap, 1  },
		{"debug-disasm",    no_argument, &cb_options.disasm,        1  },
		{"stress-gc",       no_argument, &cb_options.stress_gc,     1  },
		{"help",            no_argument, 0,                         'h'},
		{0,                 0,           0,                         0  },
	};

	int c;
	int opt_index;

	for (;;) {
		c = getopt_long(*argc, *argv, "hD", long_opts, &opt_index);
		if (c == -1)
			break;

		switch (c) {
		case 'D':
			cb_options.debug_gc = cb_options.disasm = 1;
			break;

		case 'h':
			fputs(help, stdout);
			exit(0);
			break;

		case '?':
			return 1;
		}
	}

	if (optind < *argc)
		*fname_out = (*argv)[optind++];
	else
		*fname_out = NULL;

	*argc -= optind;
	*argv += optind;

	return 0;
}

int main(int argc, char **argv) {
	int result;
	char *fname;

#ifdef PROFILE
	struct sigaction sigint_action;

	sigint_action.sa_handler = sigint_handler;
	sigemptyset(&sigint_action.sa_mask);
	sigint_action.sa_flags = 0;

	sigaction(SIGINT, &sigint_action, NULL);
#endif

	if (parse_opts(&argc, &argv, &fname))
		return 1;

	if (cb_agent_init())
		return 1;

	cb_intrinsics_set_argv(argc, argv);
	if (!fname) {
		result = cb_repl();
	} else {
		result = run_file(fname);
	}

	cb_agent_deinit();

	// puts("nops:");
	// for (int i = 0; i < OP_MAX; i += 1)
	// 	printf("\t%24s: %zu\n", cb_opcode_name(i), cb_metrics.nops[i]);

	return result;
}