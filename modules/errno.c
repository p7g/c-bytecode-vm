#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "builtin_modules.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

static size_t ident_get, ident_set, ident_perror, ident_strerror;

void cb_errno_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "get", ident_get);
	CB_DEFINE_EXPORT(spec, "set", ident_set);
	CB_DEFINE_EXPORT(spec, "perror", ident_perror);
	CB_DEFINE_EXPORT(spec, "strerror", ident_strerror);
}

static int get(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	result->type = CB_VALUE_INT;
	result->val.as_int = errno;
	return 0;
}

static int set(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	errno = argv[0].val.as_int;
	result->type = CB_VALUE_NULL;
	return 0;
}

static int wrapped_perror(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str msg;

	msg = CB_EXPECT_STRING(argv[0]);
	perror(cb_strptr(&msg));
	result->type = CB_VALUE_NULL;
	return 0;
}

static int wrapped_strerror(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	char *msg;

	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	msg = strerror(argv[0].val.as_int);

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	result->val.as_string->string = cb_str_from_cstr(msg, strlen(msg));
	return 0;
}

void cb_errno_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_get, cb_cfunc_new(ident_get, 0, get));
	CB_SET_EXPORT(mod, ident_set, cb_cfunc_new(ident_set, 0, set));
	CB_SET_EXPORT(mod, ident_perror,
			cb_cfunc_new(ident_perror, 0, wrapped_perror));
	CB_SET_EXPORT(mod, ident_strerror,
			cb_cfunc_new(ident_strerror, 0, wrapped_strerror));
}
