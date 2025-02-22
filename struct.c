#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>

#include "alloc.h"
#include "cb_util.h"
#include "gc.h"
#include "struct.h"
#include "value.h"

static void struct_spec_deinit(void *obj)
{
	struct cb_struct_spec *spec = (struct cb_struct_spec *) obj;
	if (spec->methods)
		free(spec->methods);
}

struct cb_struct_spec *cb_struct_spec_new(size_t name, size_t nfields,
		size_t nmethods)
{
	struct cb_struct_spec *spec;

	spec = cb_malloc(sizeof(struct cb_struct_spec)
			+ sizeof(size_t) * nfields, struct_spec_deinit);
	spec->name = name;
	spec->nfields = nfields;
	spec->nmethods = nmethods;
	if (nmethods > 0)
		spec->methods = calloc(nmethods, sizeof(struct cb_method));
	else
		spec->methods = NULL;

	return spec;
}

void cb_struct_spec_set_field_name(struct cb_struct_spec *spec, size_t i,
		size_t name)
{
	spec->fields[i] = name;
}

void cb_struct_spec_set_method(struct cb_struct_spec *spec, size_t i,
		size_t name, struct cb_function *func)
{
	spec->methods[i].name = name;
	spec->methods[i].func = func;
}

CB_INLINE struct cb_function *cb_struct_spec_get_method(
		struct cb_struct_spec *spec, size_t name)
{
	for (unsigned i = 0; i < spec->nmethods; i += 1) {
		if (spec->methods[i].name == name)
			return spec->methods[i].func;
	}
	return NULL;
}

void cb_struct_spec_mark(struct cb_struct_spec *spec)
{
	if (cb_gc_is_marked(&spec->gc_header))
		return;

	cb_gc_mark(&spec->gc_header);
	for (unsigned i = 0; i < spec->nmethods; i += 1) {
		if (spec->methods[i].func)
			cb_function_mark(spec->methods[i].func);
	}
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