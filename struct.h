#ifndef cb_struct_h
#define cb_struct_h

#include <sys/types.h>
#include <stdint.h>

#include "string.h"
#include "value.h"

struct cb_struct_spec {
	size_t name;
	/* A list of interned strings. The index of the string is the index of
	   the corresponding field. */
	size_t *fields, nfields;
};

struct cb_struct {
	cb_gc_header gc_header;
	const struct cb_struct_spec *spec;
	struct cb_value fields[];
};

void cb_struct_spec_init(struct cb_struct_spec *spec, size_t name);
size_t cb_struct_spec_add_field(struct cb_struct_spec *spec, size_t field);
struct cb_struct *cb_struct_spec_instantiate(
		const struct cb_struct_spec *spec);
struct cb_value *cb_struct_get_field(struct cb_struct *s, size_t name);
int cb_struct_set_field(struct cb_struct *s, size_t name, struct cb_value val);

#endif
