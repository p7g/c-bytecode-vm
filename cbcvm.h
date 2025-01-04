#ifndef cbcvm_h
#define cbcvm_h

#include "opcode.h"

extern struct cb_options {
	int debug_vm,
	    debug_gc,
	    debug_hashmap,
	    disasm,
	    stress_gc;
} cb_options;

struct cb_metrics {
	size_t nops[OP_MAX];
} cb_metrics;

void record_pattern(enum cb_opcode fst, enum cb_opcode snd);

#endif