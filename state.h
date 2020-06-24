#ifndef cb_state_h
#define cb_state_h

#include <stddef.h>

#include "value.h"

#define STACK_MAX 30000

typedef struct cb_state {
	size_t sp;
	cb_value *stack;
	cb_value *upvalues;
} cb_state;

cb_state global_state;

void cb_state_init(void);
void cb_state_deinit(void);

#endif
