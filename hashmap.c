#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#include "cb_util.h"
#include "hashmap.h"
#include "value.h"

#define MAX_LOAD_FACTOR 0.75
#define INITIAL_SIZE 64
#define NEXT_CAPACITY(C) ((C) << 1)
#define WRAP(M, I) ((I) & ((M)->size - 1))

struct entry {
	size_t key;
	unsigned char in_use;
	struct cb_value value;
};

struct cb_hashmap {
	size_t size, num_entries, version;
	struct entry *entries;
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
	m->entries = calloc(INITIAL_SIZE, sizeof(struct entry));
	m->size = INITIAL_SIZE;
	m->num_entries = 0;
	/* Initial hashmap version is 0 so zeroed out inline cache
	 * (i.e. version == 0) can mean that there is no cache. */
	m->version = 1;

	return m;
}

void cb_hashmap_free(cb_hashmap *m)
{
	free(m->entries);
	free(m);
}

static void maybe_grow(struct cb_hashmap *m)
{
	struct entry *old_entries, *entry;
	size_t old_size;
	int i;

	if (m->num_entries / m->size < MAX_LOAD_FACTOR)
		return;

	old_entries = m->entries;
	old_size = m->size;
	m->size = NEXT_CAPACITY(m->size);
	m->entries = calloc(m->size, sizeof(struct entry));
	m->num_entries = 0;
	m->version += 1;

	for (i = 0; i < old_size; i += 1) {
		entry = &old_entries[i];
		if (entry->in_use)
			cb_hashmap_set(m, entry->key, entry->value);
	}

	free(old_entries);
}

size_t cb_hashmap_find(const cb_hashmap *m, size_t key, int *empty)
{
	size_t idx;
	struct entry *entry;

	assert(empty);
	idx = hash_size_t(key);

	for (; (entry = &m->entries[WRAP(m, idx)])->in_use; idx += 1) {
		if (entry->key == key) {
			*empty = 0;
			return WRAP(m, idx);
		}
	}

	*empty = 1;
	return WRAP(m, idx);
}

int cb_hashmap_get(const cb_hashmap *m, size_t key, struct cb_value *out)
{
	ssize_t idx;
	int empty;

	idx = cb_hashmap_find(m, key, &empty);
	if (empty)
		return 0;

	if (out)
		*out = m->entries[idx].value;
	return 1;
}

void cb_hashmap_set(cb_hashmap *m, size_t key, struct cb_value value)
{
	ssize_t idx;
	struct entry *entry;
	int empty;

	maybe_grow(m);

	idx = cb_hashmap_find(m, key, &empty);
	if (empty) {
		entry = &m->entries[idx];
		assert(!entry->in_use);
		entry->in_use = 1;
		entry->key = key;
		entry->value = value;
		m->num_entries += 1;
	} else {
		entry = &m->entries[idx];
		assert(entry->in_use);
		entry->value = value;
	}
}

void cb_hashmap_mark_contents(cb_hashmap *m)
{
	size_t i;
	struct entry *entry;

	for (i = 0; i < m->size; i += 1) {
		entry = &m->entries[i];
		if (entry->in_use)
			cb_value_mark(&entry->value);
	}
}

CB_INLINE size_t cb_hashmap_version(const cb_hashmap *m)
{
	return m->version;
}

CB_INLINE struct cb_value cb_hashmap_get_index(const cb_hashmap *m,
		size_t index)
{
	assert(index < m->size);
	assert(m->entries[index].in_use);

	return m->entries[index].value;
}

/* The entry should already be in use */
CB_INLINE void cb_hashmap_set_index(cb_hashmap *m, size_t index,
		struct cb_value value)
{
	assert(index >= 0 && index < m->size);
	struct entry *entry = &m->entries[index];

	assert(entry->in_use);
	entry->value = value;
}
