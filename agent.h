#ifndef cb_agent_h
#define cb_agent_h

#include <stddef.h>

#include "module.h"
#include "string.h"

void cb_agent_init(void);
void cb_agent_deinit(void);

size_t cb_agent_intern_string(const char *str, size_t len);
cb_str cb_agent_get_string(size_t id);
int cb_agent_add_module(cb_module_spec spec);
size_t cb_agent_reserve_id();
const cb_module_spec *cb_agent_get_module(size_t id);

#endif
