#include <arpa/inet.h>
#include <stdint.h>

#include "builtin_modules.h"
#include "intrinsics.h"
#include "module.h"
#include "value.h"

static size_t ident_htonl, ident_htons, ident_ntohl, ident_ntohs;

void cb_inet_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "htonl", ident_htonl);
	CB_DEFINE_EXPORT(spec, "htons", ident_htons);
	CB_DEFINE_EXPORT(spec, "ntohl", ident_ntohl);
	CB_DEFINE_EXPORT(spec, "ntohs", ident_ntohs);
}

#define WRAP_CONV(FUNC, ARGTY) \
	static int wrapped_##FUNC(size_t argc, struct cb_value *argv, \
			struct cb_value *result) \
	{ \
		CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]); \
		result->type = CB_VALUE_INT; \
		result->val.as_int = FUNC((ARGTY) argv[0].val.as_int); \
		return 0; \
	}

WRAP_CONV(htonl, uint32_t);
WRAP_CONV(htons, uint16_t);
WRAP_CONV(ntohl, uint32_t);
WRAP_CONV(ntohs, uint16_t);

void cb_inet_instantiate(struct cb_module *mod)
{
	CB_SET_EXPORT(mod, ident_htonl,
			cb_cfunc_new(ident_htonl, 1, wrapped_htonl));
	CB_SET_EXPORT(mod, ident_htons,
			cb_cfunc_new(ident_htons, 1, wrapped_htons));
	CB_SET_EXPORT(mod, ident_ntohl,
			cb_cfunc_new(ident_ntohl, 1, wrapped_ntohl));
	CB_SET_EXPORT(mod, ident_ntohs,
			cb_cfunc_new(ident_ntohs, 1, wrapped_ntohs));
}