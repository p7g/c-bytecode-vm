#ifndef cb_gc_h
#define cb_gc_h

#include <stddef.h>

#define CB_GC_MARK(V) (((cb_gc_header *)(V))->mark = 1)

typedef void (cb_deinit_fn)(void *);

typedef struct cb_gc_header {
	struct cb_gc_header *next;
	int refcount;
	int mark;
	cb_deinit_fn *deinit;
} cb_gc_header;

void cb_gc_collect();
void cb_gc_register(cb_gc_header *obj, cb_deinit_fn *deinit_fn);

#endif
