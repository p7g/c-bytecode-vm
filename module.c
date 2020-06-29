#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include "agent.h"
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
	spec->id = cb_agent_reserve_module_id();
	spec->name = name;
	spec->exports = NULL;
	spec->exports_size = 0;
	spec->exports_len = 0;

	return spec;
}

void cb_modspec_free(struct modspec *spec)
{
	if (spec->exports)
		free(spec->exports);
	free(spec);
}

inline size_t cb_modspec_id(struct modspec *spec)
{
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

inline size_t cb_modspec_get_export_name(struct modspec *spec, size_t id)
{
	assert(id < spec->exports_len);
	return spec->exports[id];
}

size_t cb_modspec_get_export_id(struct modspec *spec, size_t name, int *ok)
{
	int i;

	for (i = 0; i < spec->exports_len; i += 1) {
		if (spec->exports[i] == name) {
			*ok = 1;
			return i;
		}
	}

	return (*ok = 0);
}

inline size_t cb_modspec_name(struct modspec *spec)
{
	return spec->name;
}
