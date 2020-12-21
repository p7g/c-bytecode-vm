#include <stdio.h>
#include <stdlib.h>

#include "agent.h"
#include "cbcvm.h"
#include "error.h"
#include "eval.h"
#include "gc.h"
#include "hashmap.h"
#include "module.h"
#include "value.h"

#define DEBUG_LOG(MSG, ...) ({ \
		if (cb_options.debug_gc) \
			printf("GC: " MSG "\n", ##__VA_ARGS__); \
	})

#define GC_HINT_THRESHOLD 2048
#define GC_INITIAL_THRESHOLD (1024 * 1024)
#define GC_HEAP_GROW_FACTOR 2

static struct cb_gc_header *allocated = NULL;
static size_t amount_allocated = 0;
static size_t next_allocation_threshold = GC_INITIAL_THRESHOLD;
static size_t hint = 0;

inline void cb_gc_register(struct cb_gc_header *obj, size_t size,
		cb_deinit_fn *deinit_fn)
{
	obj->deinit = deinit_fn;
	obj->mark = 0;
	obj->refcount = 0;
	obj->size = size;
	obj->next = allocated;
	allocated = obj;
	amount_allocated += size;
}

inline int cb_gc_should_collect(void)
{
	return hint > GC_HINT_THRESHOLD
		|| amount_allocated > next_allocation_threshold;
}

inline void cb_gc_adjust_refcount(cb_gc_header *obj, int amount)
{
	obj->refcount += amount;
	if (obj->refcount == 0) {
		hint += 1;
		if (cb_gc_should_collect()) {
			DEBUG_LOG("collecting due to refcount hint");
			cb_gc_collect();
		}
	}
}

inline void cb_gc_mark(struct cb_gc_header *obj)
{
	obj->mark = 1;
}

inline int cb_gc_is_marked(struct cb_gc_header *obj)
{
	return obj->mark;
}

static void mark(void)
{
	int i;
	struct cb_module *mod;

	/* Roots:
	 * - stack
	 * - globals
	 * - vm state error
	 */

	DEBUG_LOG("marking stack values");
	for (i = 0; i < cb_vm_state.sp; i += 1)
		cb_value_mark(&cb_vm_state.stack[i]);

	DEBUG_LOG("marking module global scopes");
	if (cb_vm_state.modules) {
		for (i = 0; i < cb_agent_modspec_count(); i += 1) {
			mod = &cb_vm_state.modules[i];
			if (!mod)
				continue;
			DEBUG_LOG("marking %s global scope", cb_strptr(
					cb_agent_get_string(cb_modspec_name(
						mod->spec))));
			cb_hashmap_mark_contents(mod->global_scope);
		}
	}

	DEBUG_LOG("marking error");
	cb_error_mark();
}

static void sweep(void)
{
	struct cb_gc_header *current, *tmp, **prev_ptr;

	current = allocated;
	prev_ptr = &allocated;
	while (current) {
		tmp = current;
		current = current->next;
		if (!tmp->mark && tmp->refcount == 0) {
			*prev_ptr = tmp->next;
			if (tmp->deinit)
				tmp->deinit(tmp);
			DEBUG_LOG("freeing object at %p", tmp);
			amount_allocated -= tmp->size;
			free(tmp);
		} else {
			DEBUG_LOG("not freeing object at %p (%s)", tmp,
					tmp->mark ? "mark" : "refcount");
			tmp->mark = 0;
			prev_ptr = &tmp->next;
		}
	}
}

void cb_gc_collect(void)
{
	size_t before = amount_allocated;
	DEBUG_LOG("start; allocated=%zu", amount_allocated);
	hint = 0;
	mark();
	sweep();
	next_allocation_threshold = amount_allocated * GC_HEAP_GROW_FACTOR;
	DEBUG_LOG("end; allocated=%zu, collected=%zu, next collection at %zu",
			amount_allocated, before - amount_allocated,
			next_allocation_threshold);
}
