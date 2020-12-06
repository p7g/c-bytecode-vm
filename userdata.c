#include <stddef.h>

#include "alloc.h"
#include "userdata.h"

inline void **cb_userdata_ptr(struct cb_userdata *data)
{
	return (void **) &data->userdata;
}

struct cb_userdata *cb_userdata_new(size_t size,
		cb_userdata_deinit_fn *deinit_fn)
{
	return cb_malloc(sizeof(struct cb_userdata) + size, deinit_fn);
}
