#ifndef cb_struct_h
#define cb_struct_h

#include <sys/types.h>
#include <stdint.h>

#include "str.h"
#include "value.h"

struct cb_struct_spec {
	cb_gc_header gc_header;
	size_t name;
	/* A list of interned strings. The index of the string is the index of
	   the corresponding field. */
	size_t nfields, fields[];
};

struct cb_struct {
	cb_gc_header gc_header;
	struct cb_struct_spec *spec;
	struct cb_value fields[];
};

struct cb_struct_spec *cb_struct_spec_new(size_t name, size_t nfields);
void cb_struct_spec_set_field_name(struct cb_struct_spec *spec, size_t i,
		size_t name);
struct cb_struct *cb_struct_spec_instantiate(struct cb_struct_spec *spec);
struct cb_value *cb_struct_get_field(struct cb_struct *s, size_t name);
int cb_struct_set_field(struct cb_struct *s, size_t name, struct cb_value val);

#endif
