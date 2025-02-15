#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "agent.h"
#include "builtin_modules.h"
#include "error.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

static size_t ident_unix,
	      ident_clock_gettime,
	      ident_CLOCK_REALTIME,
	      ident_CLOCK_MONOTONIC,
	      ident_CLOCK_PROCESS_CPUTIME_ID,
	      ident_CLOCK_THREAD_CPUTIME_ID;

void cb_time_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "unix", ident_unix);
	CB_DEFINE_EXPORT(spec, "clock_gettime", ident_clock_gettime);
	CB_DEFINE_EXPORT(spec, "CLOCK_REALTIME", ident_CLOCK_REALTIME);
	CB_DEFINE_EXPORT(spec, "CLOCK_MONOTONIC", ident_CLOCK_MONOTONIC);
	CB_DEFINE_EXPORT(spec, "CLOCK_PROCESS_CPUTIME_ID",
			ident_CLOCK_PROCESS_CPUTIME_ID);
	CB_DEFINE_EXPORT(spec, "CLOCK_THREAD_CPUTIME_ID",
			ident_CLOCK_THREAD_CPUTIME_ID);
}

static int unix_time(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	result->type = CB_VALUE_INT;
	result->val.as_int = (int64_t) time(NULL);
	return 0;
}

static int wrapped_clock_gettime(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct timespec ts;
	clockid_t clock_id;
	double val;

	if (argc > 0) {
		CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
		clock_id = (clockid_t) argv[0].val.as_int;
	} else {
		clock_id = CLOCK_MONOTONIC;
	}

	if (clock_gettime(clock_id, &ts)) {
		cb_error_from_errno();
		return 1;
	}

	val = ts.tv_sec + (double) ts.tv_nsec / 1000000000;
	result->type = CB_VALUE_DOUBLE;
	result->val.as_double = val;

	return 0;
}

#define INT(N) ({ \
		struct cb_value _v; \
		_v.type = CB_VALUE_INT; \
		_v.val.as_int = (int64_t) (N); \
		_v; \
	})

void cb_time_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_unix, cb_cfunc_new(ident_unix, 0, unix_time));
	CB_SET_EXPORT(mod, ident_clock_gettime,
			cb_cfunc_new(ident_clock_gettime, 0,
				wrapped_clock_gettime));
	CB_SET_EXPORT(mod, ident_CLOCK_REALTIME, INT(CLOCK_REALTIME));
	CB_SET_EXPORT(mod, ident_CLOCK_MONOTONIC, INT(CLOCK_MONOTONIC));
	CB_SET_EXPORT(mod, ident_CLOCK_PROCESS_CPUTIME_ID,
			INT(CLOCK_PROCESS_CPUTIME_ID));
	CB_SET_EXPORT(mod, ident_CLOCK_THREAD_CPUTIME_ID,
			INT(CLOCK_THREAD_CPUTIME_ID));
}