#ifndef cb_builtin_modules_h
#define cb_builtin_modules_h

#include "module.h"

struct cb_builtin_module_spec {
	const char *name;
	void (*build_spec)(cb_modspec *);
	void (*instantiate)(struct cb_module *);
};

const size_t cb_builtin_module_count;
struct cb_builtin_module_spec *cb_builtin_modules;

void cb_initialize_builtin_modules(void);
void cb_instantiate_builtin_modules(void);

#endif
