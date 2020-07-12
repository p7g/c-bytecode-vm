#include <assert.h>
#include <stdlib.h>

#include "alloc.h"
#include "gc.h"

#ifdef DEBUG_GC
# include <stdio.h>
#endif

void *cb_malloc(size_t bytes, cb_deinit_fn deinit_fn)
{
	void *ptr;

#ifdef STRESS_GC
	if (1) {
#else
	if (cb_gc_should_collect()) {
#endif
		cb_gc_collect();
	}

	assert(bytes >= sizeof(cb_gc_header));
	ptr = malloc(bytes);
	cb_gc_register((cb_gc_header*) ptr, bytes, deinit_fn);

#ifdef DEBUG_GC
	printf("GC: allocated %zu bytes at %p\n", bytes, ptr);
#endif

	return ptr;
}
