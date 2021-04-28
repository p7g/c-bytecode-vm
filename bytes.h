#ifndef cb_bytes_h
#define cb_bytes_h

#include "value.h"

size_t cb_bytes_len(struct cb_bytes *bs);
int16_t cb_bytes_get(struct cb_bytes *bs, size_t pos);
int cb_bytes_set(struct cb_bytes *bs, size_t pos, uint8_t value);
int cb_bytes_cmp(struct cb_bytes *a, struct cb_bytes *b);
void cb_bytes_free(struct cb_bytes *bs);
char *cb_bytes_ptr(struct cb_bytes *bs);
int cb_bytes_copy(struct cb_bytes *from, struct cb_bytes *to, size_t n);

#endif
