#ifndef cb_agent_h
#define cb_agent_h

#include <stddef.h>
#include <stdio.h>

#include "module.h"
#include "str.h"
#include "struct.h"

int cb_agent_init(void);
void cb_agent_deinit(void);

size_t cb_agent_intern_string(const char *str, size_t len);
cb_str cb_agent_get_string(size_t id);
void cb_agent_add_modspec(cb_modspec *spec);
size_t cb_agent_reserve_modspec_id();
const cb_modspec *cb_agent_get_modspec(size_t id);
cb_modspec *cb_agent_get_modspec_by_name(size_t name);
size_t cb_agent_modspec_count(void);
size_t cb_agent_add_struct_spec(struct cb_struct_spec spec);
const struct cb_struct_spec *cb_agent_get_struct_spec(size_t id);
FILE *cb_agent_resolve_import(const char *import_name, size_t len,
		const char *pwd, char **fname_out);

#ifdef DEBUG_VM
void cb_agent_set_finished_compiling();
#endif

#endif
