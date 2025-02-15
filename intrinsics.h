#ifndef cb_intrinsics_h
#define cb_intrinsics_h

#include <stdio.h>

#include "agent.h"
#include "error.h"
#include "hashmap.h"
#include "value.h"

#define CB_EXPECT_TYPE(TYPE, VAL) ({ \
		if ((VAL).type != (TYPE)) { \
			struct cb_value err; \
			cb_value_from_fmt(&err, \
					"%s: expected %s argument, got %s", \
					__func__, \
					cb_value_type_friendly_name(TYPE), \
					cb_value_type_of(&(VAL))); \
			cb_error_set(err); \
			return 1; \
		} \
	})
#define CB_EXPECT_STRING(VAL) ({ \
		cb_str _CB_EXPECT_STRING_str; \
		if ((VAL).type == CB_VALUE_STRING) { \
			_CB_EXPECT_STRING_str = (VAL).val.as_string->string; \
		} else if ((VAL).type == CB_VALUE_INTERNED_STRING) { \
			_CB_EXPECT_STRING_str = cb_agent_get_string( \
					(VAL).val.as_interned_string); \
		} else { \
			CB_EXPECT_TYPE(CB_VALUE_STRING, (VAL)); \
			return 1; \
		} \
		_CB_EXPECT_STRING_str; \
	})
#define CB_EXPECT_STRUCT(SPEC, VAL) ({ \
		struct cb_value _val = (VAL); \
		struct cb_struct_spec *_spec = (SPEC); \
		CB_EXPECT_TYPE(CB_VALUE_STRUCT, _val); \
		if (_val.val.as_struct->spec != _spec) { \
			struct cb_value err; \
			cb_value_from_fmt(&err, \
					"%s: expected %s argument, got %s", \
					__func__, \
					cb_agent_get_string(_spec->name), \
					cb_agent_get_string(_val.val.as_struct->spec->name)); \
			cb_error_set(err); \
			return 1; \
		} \
	})

void make_intrinsics(cb_hashmap *scope);
void cb_intrinsics_set_argv(int argc, char **argv);

#endif