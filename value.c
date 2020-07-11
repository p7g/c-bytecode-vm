#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "alloc.h"
#include "eval.h"
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

#ifdef DEBUG_GC
	char *as_str = cb_value_to_string(value);
	printf("GC: adjusted refcount by %d for object at %p: %s\n",
			amount, header, as_str);
	free(as_str);
#endif

	cb_gc_adjust_refcount(header, amount);
}

inline void cb_value_incref(struct cb_value *value)
{
	adjust_refcount(value, 1);
}

inline void cb_value_decref(struct cb_value *value)
{
	adjust_refcount(value, -1);
}

static void cb_function_deinit(void *ptr)
{
	int i;
	struct cb_user_function ufn;
	struct cb_function *fn = ptr;

	if (fn->type != CB_FUNCTION_USER)
		return;

	ufn = fn->value.as_user;

	if (ufn.upvalues_len) {
		for (i = 0; i < ufn.upvalues_len; i += 1)
			free(ufn.upvalues[i]);
		free(ufn.upvalues);
	}
}

inline struct cb_function *cb_function_new(void)
{
	return cb_malloc(sizeof(struct cb_function), cb_function_deinit);
}

inline struct cb_array *cb_array_new(size_t len)
{
	return cb_malloc(sizeof(struct cb_array)
			+ sizeof(struct cb_value) * len, NULL);
}

static void deinit_string(void *s_ptr)
{
	struct cb_string *s = s_ptr;

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
		len = snprintf(NULL, 0, "%" PRIdPTR, val->val.as_int);
		buf = malloc(len + 1);
		snprintf(buf, len + 1, "%" PRIdPTR, val->val.as_int);
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

	case CB_VALUE_INTERNED_STRING:
		buf = strdup(cb_strptr(cb_agent_get_string(
						val->val.as_interned_string)));
		break;

	case CB_VALUE_FUNCTION: {
		size_t name = val->val.as_function->name;
		cb_str s;
		if (name != (size_t) -1) {
			s = cb_agent_get_string(name);
			len = cb_strlen(s);
			len += sizeof("function "); /* includes NUL byte */
			buf = malloc(len);
			sprintf(buf, "function ");
			memcpy(buf + sizeof("function ") - 1, cb_strptr(s),
					cb_strlen(s));
			buf[len - 1] = 0;
		} else {
			len = sizeof("function <anonymous>") - 1;
			buf = malloc(len + 1);
			sprintf(buf, "function <anonymous>");
			buf[len] = 0;
		}
		break;
	}

	case CB_VALUE_ARRAY: {
		char *ptr;
		size_t array_len = val->val.as_array->len;
		size_t element_lens[array_len];
		char *elements[array_len];
		len = 2; /* square brackets around elements */
		for (int i = 0; i < array_len; i += 1) {
			elements[i] = cb_value_to_string(
					&val->val.as_array->values[i]);
			len += element_lens[i] = strlen(elements[i]);
			if (i != 0)
				len += 2; /* comma space */
		}
		ptr = buf = malloc(len + 1);
		buf[len] = 0;
		*ptr++ = '[';
		for (int i = 0; i < array_len; i += 1) {
			memcpy(ptr, elements[i], element_lens[i]);
			free(elements[i]);
			ptr += element_lens[i];
			if (i + 1 < array_len) {
				*ptr++ = ',';
				*ptr++ = ' ';
			}
		}
		*ptr++ = ']';
		assert(ptr == buf + len);
		break;
	}

	default:
		fprintf(stderr, "unsupported to_string\n");
		abort();
		break;
	}

	return buf;
}

int cb_value_eq(struct cb_value *a, struct cb_value *b)
{
	if (a == b)
		return 1;
	if (a->type == CB_VALUE_INTERNED_STRING && b->type == CB_VALUE_STRING) {
		return !strcmp(cb_strptr(b->val.as_string->string),
				cb_strptr(cb_agent_get_string(
						a->val.as_interned_string)));
	} else if (b->type == CB_VALUE_INTERNED_STRING
			&& a->type == CB_VALUE_STRING) {
		return !strcmp(cb_strptr(a->val.as_string->string),
				cb_strptr(cb_agent_get_string(
						b->val.as_interned_string)));
	}
	if (a->type != b->type)
		return 0;

	switch (a->type) {
	case CB_VALUE_INT:
		return a->val.as_int == b->val.as_int;
	case CB_VALUE_DOUBLE:
		return a->val.as_double == b->val.as_double;
	case CB_VALUE_BOOL:
		return a->val.as_bool == b->val.as_bool;
	case CB_VALUE_CHAR:
		return a->val.as_char == b->val.as_char;
	case CB_VALUE_NULL:
		return 1;
	case CB_VALUE_ARRAY:
		fprintf(stderr, "Checking eq of array");
		abort();
	case CB_VALUE_STRING:
		return !strcmp(cb_strptr(a->val.as_string->string),
				cb_strptr(b->val.as_string->string));
	case CB_VALUE_FUNCTION:
		if (a->val.as_function->type != b->val.as_function->type)
			return 0;
		if (a->val.as_function->type == CB_FUNCTION_USER)
			return a->val.as_function->value.as_user.address
				== b->val.as_function->value.as_user.address;
		return a->val.as_function->value.as_native
			== b->val.as_function->value.as_native;
	case CB_VALUE_INTERNED_STRING:
		return a->val.as_interned_string == b->val.as_interned_string;
	default:
		return 0;
	}
}

/* There is only a partial ordering defined for values. If ok is set to true
 * when this function returns, it means the result is the actual ordering
 * between the values `a` and `b`. Otherwise, the return value will be 0.
 *
 * `ok` can be NULL for convenience. If two values are not comparable, the
 * result will be 0 (i.e. neutral).
 *
 * If the result is negative, `a` is less than `b`. If the value is positive,
 * the reverse is true. Otherwise, if the result is zero, the values are equal
 * (or the ordering is undefined).
 */
int cb_value_cmp(struct cb_value *a, struct cb_value *b, int *ok)
{
#define UNDEFINED() ({ \
		if (ok != NULL) \
			*ok = 0; \
		0; \
	})
#define OK(VAL) ({ \
		if (ok != NULL) \
			*ok = 1; \
		(VAL); \
	})

	if (cb_value_eq(a, b))
		return OK(0);

	switch (a->type) {
	case CB_VALUE_INT:
		if (b->type == CB_VALUE_INT)
			return OK(a->val.as_int - b->val.as_int);
		if (b->type == CB_VALUE_DOUBLE)
			return OK(((double) a->val.as_int) - b->val.as_double);
		return UNDEFINED();
	case CB_VALUE_DOUBLE:
		if (b->type == CB_VALUE_INT)
			return OK(a->val.as_double - (double) b->val.as_int);
		if (b->type == CB_VALUE_DOUBLE)
			return OK(a->val.as_double - b->val.as_double);
		return UNDEFINED();

	default:
		return UNDEFINED();
	}

#undef UNDEFINED
#undef OK
}

void cb_function_add_upvalue(struct cb_user_function *fn, struct cb_upvalue *uv)
{
	if (fn->upvalues_len >= fn->upvalues_size) {
		/* FIXME: this could probably be pre-allocated; we know how many
		 * upvalues a function has when compiling, so the information
		 * just needs to be passed through the bytecode */
		if (fn->upvalues_size == 0)
			fn->upvalues_size = 4;
		else
			fn->upvalues_size <<= 1;
		fn->upvalues = realloc(fn->upvalues,
				fn->upvalues_size * sizeof(struct cb_upvalue *));
	}
	fn->upvalues[fn->upvalues_len++] = uv;
}

const char *cb_value_type_friendly_name(enum cb_value_type typ)
{
	switch (typ) {
	case CB_VALUE_INT:
		return "integer";
	case CB_VALUE_DOUBLE:
		return "double";
	case CB_VALUE_BOOL:
		return "boolean";
	case CB_VALUE_NULL:
		return "null";
	case CB_VALUE_STRING:
	case CB_VALUE_INTERNED_STRING:
		return "string";
	case CB_VALUE_CHAR:
		return "char";
	case CB_VALUE_ARRAY:
		return "array";
	case CB_VALUE_FUNCTION:
		return "function";
	}

	return "";
}

inline const char *cb_value_type_of(struct cb_value *val)
{
	return cb_value_type_friendly_name(val->type);
}

int cb_value_call(struct cb_value fn, struct cb_value *args, size_t args_len,
		struct cb_value *result)
{
	struct cb_function *func;

	if (fn.type != CB_VALUE_FUNCTION) {
		fprintf(stderr, "Value of type %s is not callable\n",
				cb_value_type_of(&fn));
		return 1;
	}

	func = fn.val.as_function;

	if (func->type == CB_FUNCTION_NATIVE)
		return func->value.as_native(args_len, args, result);
	else
		return cb_vm_call_user_func(fn, args, args_len, result);
}

void cb_value_mark(struct cb_value *val)
{
	switch (val->type) {
	case CB_VALUE_INT:
	case CB_VALUE_DOUBLE:
	case CB_VALUE_BOOL:
	case CB_VALUE_CHAR:
	case CB_VALUE_NULL:
	case CB_VALUE_INTERNED_STRING:
		return;

	case CB_VALUE_STRING:
		if (cb_gc_is_marked((cb_gc_header *) val->val.as_string))
			break;
#ifdef DEBUG_GC
		printf("GC: marked value at %p: \"%s\"\n", val->val.as_string,
				cb_strptr(val->val.as_string->string));
#endif
		cb_gc_mark((cb_gc_header *) val->val.as_string);
		break;

	case CB_VALUE_ARRAY: {
		int i;
		if (cb_gc_is_marked((cb_gc_header *) val->val.as_array))
			break;
#ifdef DEBUG_GC
		char *as_str = cb_value_to_string(val);
		printf("GC: marked value at %p: %s\n", val->val.as_array,
				as_str);
		free(as_str);
#endif
		cb_gc_mark((cb_gc_header *) val->val.as_array);
		for (i = 0; i < val->val.as_array->len; i += 1)
			cb_value_mark(&val->val.as_array->values[i]);
		break;
	}

	case CB_VALUE_FUNCTION: {
		int i;
		struct cb_function *fn;
		struct cb_upvalue *uv;

		fn = val->val.as_function;
		if (cb_gc_is_marked((cb_gc_header *) fn))
			break;
#ifdef DEBUG_GC
		char *as_str = cb_value_to_string(val);
		printf("GC: marked value at %p: %s\n", val->val.as_function,
				as_str);
		free(as_str);
#endif
		cb_gc_mark((cb_gc_header *) fn);
		if (fn->type == CB_FUNCTION_USER) {
			for (i = 0; i < fn->value.as_user.upvalues_len; i += 1) {
				uv = fn->value.as_user.upvalues[i];
				assert(!uv->is_open);
				cb_value_mark(&uv->v.value);
			}
		}
		break;
	}
	}
}
