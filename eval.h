#ifndef cb_eval_h
#define cb_eval_h

#include <stddef.h>

#include "state.h"

struct cb_frame {
	size_t prev_ip,
	       prev_bp,
	       num_args,
	       module_id,
	       current_function;
};

void cb_eval(uint8_t *code, size_t code_len);

#endif
