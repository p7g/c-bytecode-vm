#include <assert.h>
#include <stdlib.h>

#include "alloc.h"
#include "gc.h"

void *cb_malloc(size_t bytes, cb_deinit_fn deinit_fn)
{
	void *ptr;

	assert(bytes >= sizeof(cb_gc_header));
	ptr = malloc(bytes);
	cb_gc_register((cb_gc_header*) ptr, deinit_fn);

	return ptr;
}
