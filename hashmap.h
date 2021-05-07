#ifndef cb_hashmap_h
#define cb_hashmap_h

#include "value.h"

typedef struct cb_hashmap cb_hashmap;

cb_hashmap *cb_hashmap_new();
void cb_hashmap_free(cb_hashmap *map);
void cb_hashmap_mark_contents(cb_hashmap *map);
int cb_hashmap_get(cb_hashmap *m, size_t key, struct cb_value *out);
void cb_hashmap_set(cb_hashmap *map, size_t key, struct cb_value value);

#endif
