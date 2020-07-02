#include "gc.h"

static struct cb_gc_header *allocated;

void cb_gc_register(struct cb_gc_header *obj, cb_deinit_fn *deinit_fn)
{
	obj->deinit = deinit_fn;
	obj->mark = 0;
	obj->refcount = 0;
	obj->next = allocated;
	allocated = obj;
}
