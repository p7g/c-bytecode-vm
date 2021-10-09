#ifndef cb_gc_h
#define cb_gc_h

#include <stddef.h>

typedef void (cb_deinit_fn)(void *);

typedef struct cb_gc_header {
	struct cb_gc_header *next;
	int refcount;
	int mark;
	size_t size;
	cb_deinit_fn *deinit;
} cb_gc_header;

void cb_gc_mark(cb_gc_header *obj);
int cb_gc_is_marked(cb_gc_header *obj);
void cb_gc_adjust_refcount(cb_gc_header *obj, int amount);
void cb_gc_collect(void);
int cb_gc_should_collect(void);
void cb_gc_register(cb_gc_header *obj, size_t size, cb_deinit_fn *deinit_fn);

#define cb_gc_size(header) ((header)->size)

struct cb_value;
void cb_gc_queue_mark(struct cb_value *obj);

#endif
