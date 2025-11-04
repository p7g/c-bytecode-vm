#ifndef cb_alloc_h
#define cb_alloc_h

#include <stddef.h>

void *cb_calloc(size_t size, size_t count);
void *cb_malloc(size_t size);
void cb_free(void *ptr);

#endif