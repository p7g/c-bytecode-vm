#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
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
		for (i = 0; i < agent.next_module_id; i += 1)
			cb_modspec_free(agent.modules[i]);
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

inline size_t cb_agent_reserve_module_id()
{
	return agent.next_module_id++;
}

void cb_agent_add_module(cb_modspec *spec)
{
	if (agent.next_module_id >= agent.modules_size) {
		agent.modules_size = agent.modules_size == 0
			? 4
			: agent.modules_size << 1;
		agent.modules = realloc(agent.modules,
				agent.modules_size * sizeof(cb_modspec *));
	}

	agent.modules[agent.next_module_id++] = spec;
}
