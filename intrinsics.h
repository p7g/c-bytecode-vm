#ifndef cb_intrinsics_h
#define cb_intrinsics_h

#include <stdio.h>

#include "agent.h"
#include "error.h"
#include "hashmap.h"
#include "value.h"

#define CB_EXPECT_TYPE(TYPE, VAL) ({ \
		if ((VAL).type != (TYPE)) { \
			cb_error_set(cb_value_from_fmt( \
					"%s: expected %s argument, got %s", \
					__func__, \
					cb_value_type_friendly_name(TYPE), \
					cb_value_type_of(&(VAL)))); \
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

void make_intrinsics(cb_hashmap *scope);
void cb_intrinsics_set_argv(int argc, char **argv);

#endif
