#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "alloc.h"

void *cb_calloc(size_t size, size_t count)
{
	return calloc(size, count);
}

void *cb_malloc(size_t size)
{
	return malloc(size);
}

void cb_free(void *ptr)
{
	free(ptr);
}