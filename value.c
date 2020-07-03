#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "alloc.h"
#include "string.h"
#include "value.h"

static void adjust_refcount(struct cb_value *value, int amount)
{
	cb_gc_header *header;

	switch (value->type) {
	case CB_VALUE_ARRAY:
		header = &value->val.as_array->gc_header;
		break;
	case CB_VALUE_STRING:
		header = &value->val.as_string->gc_header;
		break;
	case CB_VALUE_FUNCTION:
		header = &value->val.as_function->gc_header;
		break;

	default:
		return;
	}

	header->refcount += amount;
}

inline void cb_value_incref(struct cb_value *value)
{
	adjust_refcount(value, 1);
}

inline void cb_value_decref(struct cb_value *value)
{
	adjust_refcount(value, -1);
}

inline struct cb_function *cb_function_new(void)
{
	return cb_malloc(sizeof(struct cb_function), NULL);
}

inline struct cb_array *cb_array_new(size_t len)
{
	return cb_malloc(sizeof(struct cb_array)
			+ sizeof(struct cb_value) * len, NULL);
}

static void deinit_string(void *s_ptr)
{
	struct cb_string *s = s;

	cb_str_free(s->string);
}

inline struct cb_string *cb_string_new(void)
{
	return cb_malloc(sizeof(struct cb_string), deinit_string);
}

int cb_value_is_truthy(struct cb_value *val)
{
	switch (val->type) {
	case CB_VALUE_INT:
		return val->val.as_int != 0;
	case CB_VALUE_DOUBLE:
		return val->val.as_double != 0;
	case CB_VALUE_BOOL:
		return val->val.as_bool;
	case CB_VALUE_CHAR:
		return 1;
	case CB_VALUE_STRING:
		return val->val.as_string->string.len > 0;
	case CB_VALUE_INTERNED_STRING: {
		cb_str s = cb_agent_get_string(val->val.as_interned_string);
		return s.len > 0;
	}
	case CB_VALUE_ARRAY:
		return val->val.as_array->len > 0;
	case CB_VALUE_FUNCTION:
		return 1;
	case CB_VALUE_NULL:
		return 0;
	}
	return 0;
}

const char *cb_value_type_name(enum cb_value_type type)
{
	switch (type) {
#define CASE(T) case T: return #T;
	CB_VALUE_TYPE_LIST(CASE)
#undef CASE
	default:
		return "";
	}
}

char *cb_value_to_string(struct cb_value *val)
{
	size_t len;
	char *buf;

	switch (val->type) {
	case CB_VALUE_INT:
		len = snprintf(NULL, 0, "%" PRId64, val->val.as_int);
		buf = malloc(len + 1);
		snprintf(buf, len + 1, "%" PRId64, val->val.as_int);
		buf[len] = 0;
		break;

	case CB_VALUE_DOUBLE:
		len = snprintf(NULL, 0, "%f", val->val.as_double);
		buf = malloc(len + 1);
		snprintf(buf, len + 1, "%f", val->val.as_double);
		buf[len] = 0;
		break;

	case CB_VALUE_BOOL:
		if (val->val.as_bool) {
			len = sizeof("true") - 1;
			buf = malloc(len + 1);
			sprintf(buf, "true");
			buf[len] = 0;
		} else {
			len = sizeof("false") - 1;
			buf = malloc(len + 1);
			sprintf(buf, "false");
			buf[len] = 0;
		}
		break;

	case CB_VALUE_NULL:
		len = 4;
		buf = malloc(5);
		sprintf(buf, "null");
		buf[len] = 0;
		break;

	case CB_VALUE_CHAR:
		buf = malloc(2);
		/* FIXME: unicode */
		buf[0] = val->val.as_char & 0xFF;
		buf[1] = 0;
		break;

	case CB_VALUE_STRING:
		buf = strdup(cb_strptr(val->val.as_string->string));
		break;

	case CB_VALUE_FUNCTION: {
		size_t name = val->val.as_function->name;
		if (name != (size_t) -1) {
			buf = strdup(cb_strptr(cb_agent_get_string(name)));
		} else {
			len = sizeof("<anonymous>") - 1;
			buf = malloc(len + 1);
			sprintf(buf, "<anonymous>");
			buf[len] = 0;
		}
		break;
	}

	default:
		fprintf(stderr, "unsupported to_string\n");
		abort();
		break;
	}

	return buf;
}
