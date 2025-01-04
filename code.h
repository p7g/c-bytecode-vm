#ifndef cb_code_h
#define cb_code_h

#include <stdint.h>

#include "compiler.h"
#include "constant.h"
#include "gc.h"
#include "module.h"

struct cb_code {
	/* FIXME: is it GCed or not??? */
	cb_gc_header gc_header;
	cb_modspec *modspec;
	uint16_t nlocals,
		 nupvalues,
		 nconsts,
		 stack_size;
	cb_instruction *bytecode;
	size_t bytecode_len;
	struct cb_const *const_pool;
};

struct cb_code *cb_code_new(void);
void cb_code_free(struct cb_code *code);

#endif