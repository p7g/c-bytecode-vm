#ifndef cb_agent_h
#define cb_agent_h

#include <stddef.h>

#include "module.h"
#include "string.h"

void cb_agent_init(void);
void cb_agent_deinit(void);

size_t cb_agent_intern_string(const char *str, size_t len);
cb_str cb_agent_get_string(size_t id);
void cb_agent_add_module(cb_modspec *spec);
size_t cb_agent_reserve_module_id();
const cb_modspec *cb_agent_get_module(size_t id);
cb_modspec *cb_agent_get_module_by_name(size_t name);

#endif
