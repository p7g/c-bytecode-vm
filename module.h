#ifndef cb_module_h
#define cb_module_h

#include <stddef.h>

typedef struct modspec cb_modspec;

cb_modspec *cb_modspec_new(size_t name);
void cb_modspec_free(cb_modspec *spec);
size_t cb_modspec_id(cb_modspec *spec);
size_t cb_modspec_add_export(cb_modspec *spec, size_t name);
size_t cb_modspec_get_export_name(cb_modspec *spec, size_t id);
size_t cb_modspec_get_export_id(cb_modspec *spec, size_t name, int *ok);
size_t cb_modspec_name(cb_modspec *spec);

#endif
