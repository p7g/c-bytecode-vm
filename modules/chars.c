#include "utf8proc/utf8proc.h"

#include "builtin_modules.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

static int to_lowercase_impl(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	utf8proc_int32_t c;
	CB_EXPECT_TYPE(CB_VALUE_CHAR, argv[0]);
	c = argv[0].val.as_char;
	result->type = CB_VALUE_CHAR;
	result->val.as_char = utf8proc_tolower(c);
	return 0;
}

static int to_uppercase_impl(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	utf8proc_int32_t c;
	CB_EXPECT_TYPE(CB_VALUE_CHAR, argv[0]);
	c = argv[0].val.as_char;
	result->type = CB_VALUE_CHAR;
	result->val.as_char = utf8proc_toupper(c);
	return 0;
}

size_t ident_to_lowercase, ident_to_uppercase;

void cb_chars_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "to_lowercase", ident_to_lowercase);
	CB_DEFINE_EXPORT(spec, "to_uppercase", ident_to_uppercase);
}

void cb_chars_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT_FN(mod, ident_to_lowercase, 1, to_lowercase_impl);
	CB_SET_EXPORT_FN(mod, ident_to_uppercase, 1, to_uppercase_impl);
}