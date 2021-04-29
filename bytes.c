#include "assert.h"
#include "string.h"

#include <alloc.h>
#include <bytes.h>
#include <gc.h>

int cb_bytes_cmp(struct cb_bytes *a, struct cb_bytes *b)
{
	size_t len_a, len_b;
	len_a = cb_bytes_len(a);
	len_b = cb_bytes_len(b);

	if (len_a < len_b)
		return -1;
	else if (len_a > len_b)
		return 1;

	return strncmp((char *) a->data, (char *) b->data, len_a);
}

size_t cb_bytes_len(struct cb_bytes *bs)
{
	return cb_gc_size(&bs->gc_header) - sizeof(struct cb_bytes);
}

int16_t cb_bytes_get(struct cb_bytes *bs, size_t pos)
{
	if (pos < 0 || pos >= cb_bytes_len(bs))
		return -1;
	return bs->data[pos];
}

int cb_bytes_set(struct cb_bytes *bs, size_t pos, uint8_t value)
{
	if (pos < 0 || pos >= cb_bytes_len(bs))
		return -1;
	bs->data[pos] = value;
	return 0;
}

char *cb_bytes_ptr(struct cb_bytes *bs)
{
	return (char *) bs->data;
}

int cb_bytes_copy(struct cb_bytes *from, struct cb_bytes *to, size_t n)
{
	if (cb_bytes_len(from) < n || cb_bytes_len(to) < n)
		return 1;
	memcpy(to->data, from->data, n);
	return 0;
}
