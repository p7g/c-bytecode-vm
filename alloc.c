#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "alloc.h"
#include "cbcvm.h"
#include "gc.h"

void *cb_malloc(size_t bytes, cb_deinit_fn deinit_fn)
{
	void *ptr;

	if (cb_gc_should_collect() || cb_options.stress_gc) {
		cb_gc_collect();
	}

	assert(bytes >= sizeof(cb_gc_header));
	ptr = malloc(bytes);
	cb_gc_register((cb_gc_header*) ptr, bytes, deinit_fn);

	/* if (cb_options.debug_gc) */
	/* 	printf("GC: allocated %zu bytes at %p\n", bytes, ptr); */

	return ptr;
}