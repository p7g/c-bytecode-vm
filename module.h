#ifndef cb_module_h
#define cb_module_h

#include <stddef.h>

typedef struct cb_module_spec {
	size_t id;
	size_t name;
	size_t *exports;
	size_t next_export_id;
} cb_module_spec;

size_t cb_module_add_export(cb_module_spec *spec, size_t name);
size_t cb_module_get_export_name(cb_module_spec *spec, size_t id);

#endif
