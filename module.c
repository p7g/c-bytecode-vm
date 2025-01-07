#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include "agent.h"
#include "cb_util.h"
#include "module.h"

struct modspec {
	size_t id;
	size_t name;
	size_t *exports;
	size_t exports_size;
	size_t exports_len;
};

struct modspec *cb_modspec_new(size_t name)
{
	struct modspec *spec;

	spec = malloc(sizeof(struct modspec));
	spec->id = cb_agent_reserve_modspec_id();
	spec->name = name;
	spec->exports = NULL;
	spec->exports_size = 0;
	spec->exports_len = 0;

	return spec;
}

void cb_modspec_free(struct modspec *spec)
{
	cb_agent_clear_modspec(spec);
	if (spec->exports)
		free(spec->exports);
	free(spec);
}

CB_INLINE size_t cb_modspec_id(const struct modspec *spec)
{
	assert(spec);
	return spec->id;
}

size_t cb_modspec_add_export(struct modspec *spec, size_t name)
{
	size_t id;

	if (spec->exports_len >= spec->exports_size) {
		spec->exports_size = spec->exports_size == 0
			? 4
			: spec->exports_size << 1;
		spec->exports = realloc(spec->exports,
				spec->exports_size * sizeof(size_t));
	}

	id = spec->exports_len++;
	spec->exports[id] = name;

	return id;
}

CB_INLINE size_t cb_modspec_get_export_name(const struct modspec *spec, size_t id)
{
	assert(id < spec->exports_len);
	return spec->exports[id];
}

size_t cb_modspec_get_export_id(const struct modspec *spec, size_t name,
		int *ok)
{
	for (unsigned i = 0; i < spec->exports_len; i += 1) {
		if (spec->exports[i] == name) {
			*ok = 1;
			return i;
		}
	}

	return (*ok = 0);
}

CB_INLINE size_t cb_modspec_name(const struct modspec *spec)
{
	return spec->name;
}

CB_INLINE size_t cb_modspec_n_exports(const cb_modspec *spec)
{
	return spec->exports_len;
}

int cb_module_is_zero(struct cb_module m)
{
	return m.global_scope == 0 && m.spec == 0 && m.evaluated == 0;
}

void cb_module_zero(struct cb_module *m)
{
	m->global_scope = 0;
	m->spec = 0;
	m->evaluated = 0;
}

void cb_module_free(struct cb_module *module)
{
	/* modspec belongs to agent, we can't free it here */
	cb_hashmap_free(module->global_scope);
	module->global_scope = NULL;
}