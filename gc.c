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

#define GC_HINT_THRESHOLD (1 << 14)
#define GC_INITIAL_THRESHOLD (1 << 16)
#define GC_HEAP_GROW_FACTOR 16

static struct cb_gc_header *allocated = NULL;
static size_t amount_allocated = 0;
static size_t next_allocation_threshold = GC_INITIAL_THRESHOLD;
static size_t hint = 0;

inline void cb_gc_register(struct cb_gc_header *obj, size_t size,
		cb_deinit_fn *deinit_fn)
{
	obj->deinit = deinit_fn;
	obj->mark = 0;
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

inline void cb_gc_mark(struct cb_gc_header *obj)
{
	obj->mark = 1;
}

inline int cb_gc_is_marked(struct cb_gc_header *obj)
{
	return obj->mark;
}

struct mark_queue_node {
	struct cb_value *val;
	struct mark_queue_node *next;
};

static struct mark_queue_node *mark_queue = NULL;

void cb_gc_queue_mark(struct cb_value *obj)
{
	struct mark_queue_node *node;

	node = malloc(sizeof(struct mark_queue_node));
	node->next = mark_queue;
	node->val = obj;

	mark_queue = node;
}

static void evaluate_mark_queue(void)
{
	struct mark_queue_node *tmp;

	while (mark_queue) {
		tmp = mark_queue;
		mark_queue = tmp->next;
		if (!cb_value_is_marked(tmp->val))
			cb_value_mark(tmp->val);
		free(tmp);
	}
}

struct cext_root {
	struct cext_root *prev, *next;
	struct cb_value value;
};

static struct cext_root *cext_root = NULL;

struct cext_root *cb_gc_hold(struct cb_value value)
{
	struct cext_root *node = malloc(sizeof(struct cext_root));
	node->prev = NULL;
	node->next = cext_root;
	node->value = value;
	cext_root = node;
	return node;
}

void cb_gc_release(struct cext_root *node)
{
	if (node->prev)
		node->prev->next = node->next;
	if (node->next)
		node->next->prev = node->prev;
	if (node == cext_root)
		cext_root = node->next;
	free(node);
}

static void mark(void)
{
	struct cb_module *mod;

	/* Roots:
	 * - stack
	 * - globals
	 * - vm state error
	 * - functions in frames
	 */

	DEBUG_LOG("marking stack values");
	for (struct cb_value *val = cb_vm_state.stack;
			val < cb_vm_state.stack_top; val += 1)
		cb_value_mark(val);

	DEBUG_LOG("marking module global scopes");
	if (cb_vm_state.modules) {
		for (unsigned i = 0; i < cb_agent_modspec_count(); i += 1) {
			mod = &cb_vm_state.modules[i];
			if (!mod || !mod->spec)
				continue;
			cb_str name = cb_agent_get_string(cb_modspec_name(
						mod->spec));
			DEBUG_LOG("marking %s global scope", cb_strptr(&name));
			cb_hashmap_mark_contents(mod->global_scope);
		}
	}

	DEBUG_LOG("marking error");
	cb_error_mark();

	DEBUG_LOG("marking frame functions");
	for (struct cb_frame *f = cb_vm_state.frame; f; f = f->parent) {
		if (CB_VALUE_IS_USER_FN(&f->func))
			cb_value_mark(&f->func);
	}

	DEBUG_LOG("marking c ext roots");
	for (struct cext_root *node = cext_root; node; node = node->next)
		cb_value_mark(&node->value);

	evaluate_mark_queue();
}

static void sweep(void)
{
	struct cb_gc_header *current, *tmp, **prev_ptr;

	current = allocated;
	prev_ptr = &allocated;
	while (current) {
		tmp = current;
		current = current->next;
		if (!tmp->mark) {
			*prev_ptr = tmp->next;
			if (tmp->deinit)
				tmp->deinit(tmp);
			DEBUG_LOG("freeing object at %p", tmp);
			amount_allocated -= tmp->size;
			free(tmp);
		} else {
			DEBUG_LOG("not freeing object at %p", tmp);
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