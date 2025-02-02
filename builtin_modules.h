#ifndef cb_builtin_modules_h
#define cb_builtin_modules_h

#include "agent.h"
#include "hashmap.h"
#include "module.h"

#define CB_DEFINE_EXPORT(SPEC, NAME, VAR) \
	cb_modspec_add_export((SPEC), \
		((VAR) = cb_agent_intern_string((NAME), sizeof((NAME)) - 1)))
#define CB_SET_EXPORT(MOD, NAME, VAL) \
	cb_hashmap_set((MOD)->global_scope, (NAME), (VAL));

struct cb_builtin_module_spec {
	const char *name;
	void (*build_spec)(cb_modspec *);
	void (*instantiate)(struct cb_module *);
};

extern const size_t cb_builtin_module_count;
extern const struct cb_builtin_module_spec *cb_builtin_modules;

void cb_initialize_builtin_modules(void);
void cb_instantiate_builtin_modules(void);

struct cb_struct_spec *cb_declare_struct(const char *name, unsigned nfields, ...);
struct cb_struct *cb_struct_make(struct cb_struct_spec *spec, ...);

#endif