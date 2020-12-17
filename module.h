#ifndef cb_module_h
#define cb_module_h

#include <stddef.h>

#include "hashmap.h"

typedef struct modspec cb_modspec;

struct cb_module {
	const struct modspec *spec;
	cb_hashmap *global_scope;
};

cb_modspec *cb_modspec_new(size_t name);
void cb_modspec_free(cb_modspec *spec);
size_t cb_modspec_id(const cb_modspec *spec);
size_t cb_modspec_add_export(cb_modspec *spec, size_t name);
size_t cb_modspec_get_export_name(const cb_modspec *spec, size_t id);
size_t cb_modspec_get_export_id(const cb_modspec *spec, size_t name, int *ok);
size_t cb_modspec_name(const cb_modspec *spec);

void cb_module_free(struct cb_module *module);
int cb_module_is_zero(struct cb_module m);

#endif
