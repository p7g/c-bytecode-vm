#ifndef cb_userdata_h
#define cb_userdata_h

#include <stddef.h>

#include "gc.h"

typedef void (cb_userdata_deinit_fn)(void *user_data);

struct cb_userdata {
	cb_gc_header gc_header;
	unsigned char userdata[];
};

void **cb_userdata_ptr(struct cb_userdata *ud);
struct cb_userdata *cb_userdata_new(size_t size,
		cb_userdata_deinit_fn *deinit_fn);

#endif
