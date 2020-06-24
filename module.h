#ifndef cb_module_h
#define cb_module_h

#include <stddef.h>

typedef struct cb_module_spec {
	size_t id;
	size_t name;
	size_t next_export_id;
} cb_module_spec;

#endif
