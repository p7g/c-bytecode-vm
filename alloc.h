#ifndef cb_alloc_h
#define cb_alloc_h

#include <stddef.h>

#include "gc.h"

void *cb_malloc(size_t bytes, cb_deinit_fn deinit_fn);

#endif
