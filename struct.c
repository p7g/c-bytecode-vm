#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>

#include "alloc.h"
#include "struct.h"
#include "value.h"

void cb_struct_spec_init(struct cb_struct_spec *spec, size_t name)
{
	spec->name = name;
	spec->nfields = 0;
	spec->fields = NULL;
}

size_t cb_struct_spec_add_field(struct cb_struct_spec *spec, size_t field)
{
	spec->nfields += 1;
	spec->fields = realloc(spec->fields, spec->nfields * sizeof(size_t));
	spec->fields[spec->nfields - 1] = field;
	return spec->nfields - 1;
}

struct cb_struct *cb_struct_spec_instantiate(const struct cb_struct_spec *spec)
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

struct cb_value *cb_struct_get_field(struct cb_struct *s, size_t name)
{
	ssize_t idx;

	idx = field_index(s->spec, name);
	if (idx > s->spec->nfields || s < 0)
		return NULL;

	return &s->fields[idx];
}

int cb_struct_set_field(struct cb_struct *s, size_t name, struct cb_value val)
{
	ssize_t idx;

	idx = field_index(s->spec, name);
	if (idx > s->spec->nfields || s < 0)
		return 1;

	s->fields[idx] = val;
	return 0;
}
