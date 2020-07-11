#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#include "hashmap.h"
#include "value.h"

#define MAX_LOAD_FACTOR 0.75
#define INITIAL_SIZE 64

struct entry {
	struct entry *next;
	size_t key;
	struct cb_value value;
};

struct cb_hashmap {
	size_t size, num_entries;
	struct entry **entries;
};

/* Hash functions courtesy of:
 * https://stackoverflow.com/a/12996028
 */
static inline size_t hash_size_t(size_t x) {
	if (sizeof(size_t) == 8) {
		x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
		x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
		x = x ^ (x >> 31);
		return x;
	} else {
		x = ((x >> 16) ^ x) * 0x45d9f3b;
		x = ((x >> 16) ^ x) * 0x45d9f3b;
		x = (x >> 16) ^ x;
		return x;
	}
}

struct cb_hashmap *cb_hashmap_new(void)
{
	struct cb_hashmap *m;

	m = malloc(sizeof(struct cb_hashmap));
	m->entries = calloc(INITIAL_SIZE, sizeof(struct entry *));
	m->size = INITIAL_SIZE;
	m->num_entries = 0;

	return m;
}

void cb_hashmap_free(cb_hashmap *m)
{
	struct entry *entry, *tmp;
	int i;

	for (i = 0; i < m->size; i += 1) {
		entry = m->entries[i];
		while (entry) {
			tmp = entry;
			entry = entry->next;
			cb_value_decref(&tmp->value);
			free(tmp);
		}
	}

	free(m->entries);
	free(m);
}

static void maybe_grow(struct cb_hashmap *m)
{
	struct entry **old_entries, *entry, *tmp;
	size_t old_size;
	int i;

	if (m->num_entries / m->size < MAX_LOAD_FACTOR)
		return;

	old_entries = m->entries;
	old_size = m->size;
	m->size <<= 1;
	m->entries = calloc(m->size, sizeof(struct entry *));
	m->num_entries = 0;

	for (i = 0; i < old_size; i += 1) {
		entry = old_entries[i];
		while (entry) {
			tmp = entry;
			entry = entry->next;
			cb_hashmap_set(m, tmp->key, tmp->value);
			free(tmp);
		}
	}

	free(old_entries);
}

struct cb_value *cb_hashmap_get(cb_hashmap *m, size_t key)
{
	size_t idx;
	struct entry *entry;

	idx = hash_size_t(key) % m->size;
	entry = m->entries[idx];
	while (entry) {
		if (entry->key == key)
			return &entry->value;
		entry = entry->next;
	}
	return NULL;
}

void cb_hashmap_set(cb_hashmap *m, size_t key, struct cb_value value)
{
	size_t idx;
	struct entry *entry;

	maybe_grow(m);

	cb_value_incref(&value);
	idx = hash_size_t(key) % m->size;

	entry = m->entries[idx];
	while (entry != NULL) {
		if (entry->key == key) {
			cb_value_decref(&entry->value);
			entry->value = value;
			return;
		}
		entry = entry->next;
	}

	entry = malloc(sizeof(struct entry));
	entry->key = key;
	entry->value = value;
	entry->next = m->entries[idx];
	m->entries[idx] = entry;
}
