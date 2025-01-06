#ifndef cbcvm_h
#define cbcvm_h

#include "opcode.h"

extern struct cb_options {
	int debug_gc,
	    debug_hashmap,
	    disasm,
	    stress_gc;
} cb_options;

#endif