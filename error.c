#include <assert.h>
#include <stdlib.h>

#include "agent.h"
#include "error.h"
#include "eval.h"
#include "module.h"
#include "value.h"

struct cb_error {
	struct cb_value value;
	struct cb_traceback *tb;
};

int cb_error_p(void)
{
	return cb_vm_state.error != NULL;
}

struct cb_value cb_error_value(void)
{
	assert(cb_error_p());
	return cb_vm_state.error->value;
}

void cb_error_set(struct cb_value value)
{
	struct cb_error *err;

	err = malloc(sizeof(struct cb_error));
	err->tb = NULL;
	err->value = value;

	/* FIXME: handle errors raised while handling errors */
	if (cb_vm_state.error)
		free(cb_vm_state.error);

	cb_vm_state.error = err;
}

void cb_error_recover(void)
{
	struct cb_traceback *tb, *tmp;

	if (!cb_vm_state.error)
		return;

	tb = cb_vm_state.error->tb;
	while (tb) {
		tmp = tb->next;
		free(tb);
		tb = tmp;
	}

	free(cb_vm_state.error);
	cb_vm_state.error = NULL;
}

/* NOTE: frame is copied, ownership is not take by this function */
void cb_traceback_add_frame(struct cb_frame *frame)
{
	struct cb_traceback *tb;

	assert(cb_vm_state.error);

	tb = malloc(sizeof(struct cb_traceback));
	tb->frame = *frame;
	if (frame->is_function)
		tb->func = frame->stack[0];
	tb->next = cb_vm_state.error->tb;
	cb_vm_state.error->tb = tb;
}

void cb_traceback_print(FILE *f, struct cb_traceback *tb)
{
	struct cb_function *func;
	struct cb_user_function *ufunc;
	const cb_modspec *spec;

	if (tb->frame.is_function) {
		cb_str buf;

		func = tb->func.val.as_function;
		fputs("\tin ", f);
		/* FIXME: add module information to native functions */
		if (func->type == CB_FUNCTION_USER) {
			ufunc = &func->value.as_user;
			spec = ufunc->code->modspec;
			cb_str modname = cb_agent_get_string(
					cb_modspec_name(spec));
			fprintf(f, "%s.", cb_strptr(&modname));
		}
		buf = cb_value_to_string(tb->func);
		fprintf(f, "%s\n", cb_strptr(&buf));
		cb_str_free(buf);
	} else {
		const char *buf;

		spec = tb->frame.module->spec;
		cb_str modname = cb_agent_get_string(cb_modspec_name(spec));
		buf = cb_strptr(&modname);
		fprintf(f, "\tin module %s\n", buf);
	}
}

struct cb_traceback *cb_error_tb(void)
{
	assert(cb_vm_state.error);
	return cb_vm_state.error->tb;
}

void cb_error_mark(void)
{
	struct cb_error *e;
	struct cb_traceback *tb;

	e = cb_vm_state.error;
	if (!e)
		return;

	cb_value_mark(e->value);
	for (tb = e->tb; tb; tb = tb->next) {
		if (tb->frame.is_function)
			cb_value_mark(tb->func);
	}
}