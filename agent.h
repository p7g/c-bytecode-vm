#ifndef cb_agent_h
#define cb_agent_h

#include <stddef.h>

#include "module.h"

typedef struct cb_agent {
	const char **string_table;
	cb_module_spec *modules;
	size_t next_module_id;
} cb_agent;

cb_agent global_agent;

void cb_agent_init(void);
void cb_agent_deinit(void);

size_t cb_agent_intern_string(const char *str);
size_t cb_agent_add_module(cb_module_spec spec);
const cb_module_spec *cb_agent_get_module(size_t id);

#endif
