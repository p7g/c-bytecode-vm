#ifndef cb_code_h
#define cb_code_h

#include <stdint.h>

#include "compiler.h"
#include "constant.h"
#include "gc.h"
#include "module.h"

struct cb_loc {
	unsigned short run;
	unsigned line, column;
};

struct cb_code {
	cb_gc_header gc_header;
	cb_modspec *modspec;
	uint16_t nlocals,
		 nupvalues,
		 nconsts,
		 stack_size;
	cb_instruction *bytecode;
	size_t bytecode_len;
	struct cb_const *const_pool;
	union cb_inline_cache *ic;
	struct cb_loc *loc;
	unsigned max_try_depth;
};

struct cb_code *cb_code_new(void);
void cb_code_mark(struct cb_code *code);
cb_gc_hold_key *cb_code_gc_hold(struct cb_code *code);
void cb_code_lineno(const struct cb_code *code, size_t ip, unsigned *line,
		unsigned *column);

#endif