#ifndef cb_alloc_h
#define cb_alloc_h

#include <stddef.h>

void *cb_malloc(size_t bytes);
void *cb_realloc(void *mem, size_t new_size);
void cb_free(void *mem);

#endif
