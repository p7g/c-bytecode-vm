#ifndef cb_error_h
#define cb_error_h

#include "eval.h"
#include "value.h"

struct cb_traceback {
	struct cb_traceback *next;
	struct cb_frame frame;
	/* NOTE: null if not frame.is_function */
	struct cb_value func;
	size_t ip;
};

int cb_error_p(void);
void cb_error_set(struct cb_value value);
void cb_error_from_errno(void);
void cb_error_recover(void);
struct cb_value cb_error_value(void);
void cb_traceback_add_frame(struct cb_frame *frame, size_t ip);
struct cb_traceback *cb_error_tb(void);
void cb_traceback_print(FILE *f, struct cb_traceback *tb);

/* for GC */
void cb_error_mark(void);

#endif