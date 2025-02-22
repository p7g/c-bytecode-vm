#include <stdlib.h>

#include "alloc.h"
#include "code.h"
#include "constant.h"
#include "gc.h"
#include "module.h"
#include "struct.h"

static void code_deinit(void *code_ptr)
{
	struct cb_code *code = (struct cb_code *)code_ptr;
	free(code->bytecode);
	for (int i = 0; i < code->nconsts; i += 1)
		cb_const_free(&code->const_pool[i]);
	free(code->const_pool);
	free(code->ic);
	if (code->loc)
		free(code->loc);
}

struct cb_code *cb_code_new(void)
{
	return cb_malloc(sizeof(struct cb_code), code_deinit);
}

static void code_mark_fn(void *obj)
{
	cb_code_mark((struct cb_code *) obj);
}

static void queue_mark(struct cb_code *code)
{
	cb_gc_queue_mark((void *) code, code_mark_fn);
}

cb_gc_hold_key *cb_code_gc_hold(struct cb_code *code)
{
	return cb_gc_hold((void *) code, code_mark_fn);
}

static void mark_const(struct cb_const c)
{
	switch (c.type) {
	case CB_CONST_MODULE:
		queue_mark(cb_modspec_code(c.val.as_module));
		break;

	case CB_CONST_FUNCTION:
		queue_mark(c.val.as_function->code);
		break;

	case CB_CONST_STRUCT_SPEC:
		cb_struct_spec_mark(c.val.as_struct_spec);
		break;

	default:
		break;
	}
}

void cb_code_mark(struct cb_code *code)
{
	cb_gc_mark(&code->gc_header);

	for (unsigned i = 0; i < code->nconsts; i += 1)
		mark_const(code->const_pool[i]);
}

void cb_code_lineno(const struct cb_code *code, size_t ip, unsigned *line,
		unsigned *column)
{
	*line = *column = 0;

	size_t sum = 0;
	/* FIXME: danger */
	for (unsigned i = 0; sum < ip; i++) {
		struct cb_loc *l = &code->loc[i];
		sum += l->run;
		if (sum >= ip) {
			*line = l->line;
			*column = l->column;
			return;
		}
	}
}