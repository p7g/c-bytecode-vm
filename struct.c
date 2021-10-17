#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>

#include "alloc.h"
#include "struct.h"
#include "value.h"

struct cb_struct_spec *cb_struct_spec_new(size_t name, size_t nfields)
{
	struct cb_struct_spec *spec;

	spec = cb_malloc(sizeof(struct cb_struct_spec)
			+ sizeof(size_t) * nfields, NULL);
	spec->name = name;
	spec->nfields = nfields;

	return spec;
}

void cb_struct_spec_set_field_name(struct cb_struct_spec *spec, size_t i,
		size_t name)
{
	spec->fields[i] = name;
}

struct cb_struct *cb_struct_spec_instantiate(struct cb_struct_spec *spec)
{
	struct cb_struct *val;
	size_t i;

	val = cb_malloc(sizeof(struct cb_struct) +
			spec->nfields * sizeof(struct cb_value), NULL);
	val->spec = spec;

	for (i = 0; i < spec->nfields; i += 1)
		val->fields[i].type = CB_VALUE_NULL;

	return val;
}

static ssize_t field_index(const struct cb_struct_spec *spec, size_t name)
{
	size_t i;

	for (i = 0; i < spec->nfields; i += 1) {
		if (spec->fields[i] == name)
			return (ssize_t) i;
	}

	return -1;
}

struct cb_value *cb_struct_get_field(struct cb_struct *s, size_t name,
		ssize_t *idx_out)
{
	ssize_t idx;

	idx = field_index(s->spec, name);
	if (idx_out)
		*idx_out = idx;
	if (idx < 0 || (size_t) idx > s->spec->nfields)
		return NULL;

	return &s->fields[idx];
}

int cb_struct_set_field(struct cb_struct *s, size_t name, struct cb_value val,
		ssize_t *idx_out)
{
	ssize_t idx;

	idx = field_index(s->spec, name);
	if (idx_out)
		*idx_out = idx;
	if (idx < 0 || (size_t) idx > s->spec->nfields)
		return 1;

	s->fields[idx] = val;
	return 0;
}
