#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "module.h"
#include "string.h"

#define INITIAL_STRING_TABLE_SIZE 4

struct cb_agent {
	int inited;

	cb_str *string_table;
	cb_modspec **modules;
	size_t string_table_size,
	       modules_size;

	size_t next_module_id,
	       next_string_id;
};

static struct cb_agent agent;

void cb_agent_init(void)
{
	assert(!agent.inited);
	agent.inited = 1;

	agent.string_table = NULL;
	agent.modules = NULL;
	agent.next_module_id = 0;
	agent.next_string_id = 0;
	agent.string_table_size = 0;
	agent.modules_size = 0;
}

void cb_agent_deinit(void)
{
	int i;

	assert(agent.inited);
	agent.inited = 0;

	for (i = 0; i < agent.next_string_id; i += 1)
		cb_str_free(agent.string_table[i]);

	if (agent.string_table)
		free(agent.string_table);
	if (agent.modules) {
		for (i = 0; i < agent.next_module_id; i += 1) {
			/* Some entries in the modules array are only reserved,
			 * they don't have a modspec in them yet */
			if (agent.modules[i])
				cb_modspec_free(agent.modules[i]);
		}
		free(agent.modules);
	}

	agent.string_table = NULL;
	agent.modules = NULL;
}

size_t cb_agent_intern_string(const char *str, size_t len)
{
	size_t id;

	if (!agent.string_table) {
		agent.string_table = malloc(sizeof(cb_str)
				* INITIAL_STRING_TABLE_SIZE);
		agent.string_table_size = INITIAL_STRING_TABLE_SIZE;
	}

	for (id = 0; id < agent.next_string_id; id += 1) {
		if (cb_str_eq_cstr(agent.string_table[id], str, len))
			return id;
	}

	id = agent.next_string_id++;

	if (id >= agent.string_table_size) {
		/* need to resize */
		agent.string_table_size <<= 1;
		agent.string_table = realloc(agent.string_table,
				agent.string_table_size * sizeof(cb_str));
	}

	agent.string_table[id] = cb_str_from_cstr(str, len);

	return id;
}

/* Strings returned from this function should not be freed */
inline cb_str cb_agent_get_string(size_t id)
{
	assert(id < agent.next_string_id);

	return agent.string_table[id];
}

static void maybe_grow_modules()
{
	if (agent.next_module_id >= agent.modules_size) {
		agent.modules_size = agent.modules_size == 0
			? 4
			: agent.modules_size << 1;
		agent.modules = realloc(agent.modules,
				agent.modules_size * sizeof(cb_modspec *));
	}
}

inline void cb_agent_add_modspec(cb_modspec *spec)
{
	assert(spec != NULL);
	agent.modules[cb_modspec_id(spec)] = spec;
}

inline size_t cb_agent_reserve_modspec_id()
{
	size_t id;

	maybe_grow_modules();
	id = agent.next_module_id++;
	agent.modules[id] = NULL;
	return id;
}

inline const cb_modspec *cb_agent_get_modspec(size_t id)
{
	assert(id < agent.next_module_id);
	return agent.modules[id];
}

cb_modspec *cb_agent_get_modspec_by_name(size_t name)
{
	int i;

	for (i = 0; i < agent.next_module_id; i += 1) {
		if (!agent.modules[i])
			continue;
		if (cb_modspec_name(agent.modules[i]) == name)
			return agent.modules[i];
	}

	return NULL;
}

inline size_t cb_agent_modspec_count(void)
{
	return agent.next_module_id;
}
