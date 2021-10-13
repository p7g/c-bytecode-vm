#ifndef cb_hashmap_h
#define cb_hashmap_h

#include <sys/types.h>

#include "value.h"

typedef struct cb_hashmap cb_hashmap;

cb_hashmap *cb_hashmap_new();
void cb_hashmap_free(cb_hashmap *map);
void cb_hashmap_mark_contents(cb_hashmap *map);
int cb_hashmap_get(const cb_hashmap *m, size_t key, struct cb_value *out);
void cb_hashmap_set(cb_hashmap *map, size_t key, struct cb_value value);
size_t cb_hashmap_version(const cb_hashmap *map);
ssize_t cb_hashmap_find(const cb_hashmap *map, size_t key, int *empty);
struct cb_value cb_hashmap_get_index(const cb_hashmap *map, size_t index);
void cb_hashmap_set_index(cb_hashmap *map, size_t index, struct cb_value value);

#endif
