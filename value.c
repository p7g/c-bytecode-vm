#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "alloc.h"
#include "bytes.h"
#include "cb_util.h"
#include "cbcvm.h"
#include "constant.h"
#include "eval.h"
#include "module.h"
#include "str.h"
#include "struct.h"
#include "value.h"

static void cb_function_deinit(void *ptr)
{
	struct cb_user_function ufn;
	struct cb_function *fn = ptr;

	if (fn->type != CB_FUNCTION_USER)
		return;

	ufn = fn->value.as_user;

	if (ufn.code->nupvalues) {
		for (unsigned i = 0; i < ufn.code->nupvalues; i += 1) {
			if (ufn.upvalues[i] == NULL)
				continue;
			if (ufn.upvalues[i]->refcount != 0)
				ufn.upvalues[i]->refcount -= 1;
			if (ufn.upvalues[i]->refcount == 0)
				free(ufn.upvalues[i]);
			if (cb_vm_state.upvalues != NULL) {
				for (unsigned j = 0;
						j < cb_vm_state.upvalues_idx;
						j += 1) {
					if (cb_vm_state.upvalues[j]
							== ufn.upvalues[i]) {
						cb_vm_state.upvalues[j] = NULL;
						break;
					}
				}
			}
		}
		free(ufn.upvalues);
	}
}

CB_INLINE struct cb_function *cb_function_new(void)
{
	return cb_malloc(sizeof(struct cb_function), cb_function_deinit);
}

struct cb_value cb_cfunc_new(size_t name, size_t arity,
		cb_native_function *fn)
{
	struct cb_function *func;
	struct cb_value func_val;

	func = cb_function_new();
	func->arity = arity;
	func->name = name;
	func->type = CB_FUNCTION_NATIVE;
	func->value.as_native = fn;
	func_val.type = CB_VALUE_FUNCTION;
	func_val.val.as_function = func;

	return func_val;
}

CB_INLINE struct cb_array *cb_array_new(size_t len)
{
	struct cb_array *mem = cb_malloc(sizeof(struct cb_array)
			+ sizeof(struct cb_value) * len, NULL);
	mem->len = len;
	return mem;
}

static void deinit_string(void *s_ptr)
{
	struct cb_string *s = s_ptr;

	cb_str_free(s->string);
}

CB_INLINE struct cb_string *cb_string_new(void)
{
	return cb_malloc(sizeof(struct cb_string), deinit_string);
}

CB_INLINE int cb_value_is_truthy(struct cb_value *val)
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
		return cb_strlen(val->val.as_string->string) > 0;
	case CB_VALUE_BYTES:
		return cb_bytes_len(val->val.as_bytes) > 0;
	case CB_VALUE_INTERNED_STRING: {
		cb_str s = cb_agent_get_string(val->val.as_interned_string);
		return cb_strlen(s) > 0;
	}
	case CB_VALUE_ARRAY:
		return val->val.as_array->len > 0;
	case CB_VALUE_FUNCTION:
		return 1;
	case CB_VALUE_NULL:
		return 0;
	case CB_VALUE_STRUCT:
	case CB_VALUE_STRUCT_SPEC:
		return 1;
	case CB_VALUE_USERDATA:
		return 1;
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
		return "<invalid type>";
	}
}

/* See Py_ReprEnter in cpython/Objects/object.c for the inspiration of this */
static struct repr_stack {
	struct repr_stack *next;
	cb_gc_header *obj;
} *repr_stack = NULL;

int repr_enter(cb_gc_header *obj)
{
	struct repr_stack *tmp;

	for (tmp = repr_stack; tmp; tmp = tmp->next) {
		if (tmp->obj == obj)
			return 1;
	}

	tmp = malloc(sizeof(struct repr_stack));
	tmp->next = repr_stack;
	tmp->obj = obj;
	repr_stack = tmp;

	return 0;
}

void repr_leave(cb_gc_header *obj)
{
	struct repr_stack *tmp, **prev;

	for (prev = &repr_stack, tmp = repr_stack;
			tmp && tmp->obj != obj;
			prev = &tmp->next, tmp = tmp->next);
	if (tmp != NULL) {
		*prev = tmp->next;
		free(tmp);
	}
}

cb_str cb_value_to_string(struct cb_value val)
{
	size_t len;
	cb_str buf;

	switch (val.type) {
	case CB_VALUE_INT:
		len = snprintf(NULL, 0, "%" PRIdPTR, val.val.as_int);
		cb_str_init(&buf, len);
		snprintf(cb_strptr(&buf), len + 1, "%" PRIdPTR, val.val.as_int);
		break;

	case CB_VALUE_DOUBLE:
		len = snprintf(NULL, 0, "%.12g", val.val.as_double);
		cb_str_init(&buf, len);
		snprintf(cb_strptr(&buf), len + 1, "%.12g", val.val.as_double);
		break;

	case CB_VALUE_BOOL:
		if (val.val.as_bool) {
			len = sizeof("true") - 1;
			cb_str_init(&buf, len);
			snprintf(cb_strptr(&buf), len + 1, "true");
		} else {
			len = sizeof("false") - 1;
			cb_str_init(&buf, len);
			snprintf(cb_strptr(&buf), len + 1, "false");
		}
		break;

	case CB_VALUE_NULL:
		len = 4;
		cb_str_init(&buf, len);
		snprintf(cb_strptr(&buf), len + 1, "null");
		break;

	case CB_VALUE_CHAR:
		cb_str_init(&buf, 1);
		/* FIXME: unicode */
		cb_strptr(&buf)[0] = val.val.as_char & 0xFF;
		cb_strptr(&buf)[1] = 0;
		break;

	case CB_VALUE_STRING:
		buf = cb_strdup(val.val.as_string->string);
		break;

	case CB_VALUE_BYTES: {
		char *ptr, *start;
		size_t i, blen;
		struct cb_bytes *bs = val.val.as_bytes;

		blen = cb_bytes_len(bs);
		/* room for "<<>>" plus comma-space between each elem */
		len = 4;
		if (blen > 0)
			len += (blen - 1) * 2;
		for (i = 0; i < blen; i += 1)
			len += snprintf(NULL, 0, "%d", cb_bytes_get(bs, i));
		cb_str_init(&buf, len);
		start = ptr = cb_strptr(&buf);
		*ptr++ = '<';
		*ptr++ = '<';
		for (i = 0; i < blen; i += 1) {
			ptr += snprintf(ptr, (len + 1) - (ptr - start), "%d", cb_bytes_get(bs, i));
			if (i + 1 < blen) {
				*ptr++ = ',';
				*ptr++ = ' ';
			}
		}
		*ptr++ = '>';
		*ptr++ = '>';
		*ptr++ = 0;
		assert(ptr == start + len + 1);
		break;
	}

	case CB_VALUE_INTERNED_STRING:
		buf = cb_strdup(cb_agent_get_string(
					val.val.as_interned_string));
		break;

	case CB_VALUE_FUNCTION: {
		const struct cb_function *func = val.val.as_function;
		size_t name = func->name;
		cb_str s, modname;
		cb_str_init(&modname, 0);
		s = cb_agent_get_string(name);
		len = cb_strlen(s);
		len += sizeof("<function >"); /* includes NUL byte */
		/* User functions show the module name too */
		if (func->type == CB_FUNCTION_USER) {
			const struct cb_user_function *ufunc;
			ufunc = &func->value.as_user;
			modname = cb_agent_get_string(
					cb_modspec_name(ufunc->code->modspec));
			/* modname + '.' */
			len += 1 + cb_strlen(modname);
		}
		cb_str_init(&buf, len - 1);
		snprintf(cb_strptr(&buf), sizeof("<function "), "<function ");
		size_t pos = sizeof("<function ") - 1;
		if (cb_strlen(modname)) {
			memcpy(cb_strptr(&buf) + pos,
					cb_strptr(&modname),
					cb_strlen(modname));
			pos += cb_strlen(modname);
			cb_strptr(&buf)[pos] = '.';
			pos += 1;
		}
		memcpy(cb_strptr(&buf) + pos, cb_strptr(&s),
				cb_strlen(s));
		cb_strptr(&buf)[len - 2] = '>';
		cb_strptr(&buf)[len - 1] = 0;
		break;
	}

	case CB_VALUE_ARRAY: {
		char *ptr;
		size_t array_len = val.val.as_array->len;
		cb_str *elements = alloca(sizeof(cb_str) * array_len);

		if (repr_enter(&val.val.as_array->gc_header)) {
			len = sizeof("[...]") - 1;
			cb_str_init(&buf, len);
			memcpy(cb_strptr(&buf), "[...]", len);
			break;
		}

		len = 2; /* square brackets around elements */
		for (unsigned i = 0; i < array_len; i += 1) {
			elements[i] = cb_value_to_string(
					val.val.as_array->values[i]);
			len += cb_strlen(elements[i]);
			if (i != 0)
				len += 2; /* comma space */
		}
		cb_str_init(&buf, len);
		ptr = cb_strptr(&buf);
		*ptr++ = '[';
		for (unsigned i = 0; i < array_len; i += 1) {
			memcpy(ptr, cb_strptr(&elements[i]),
					cb_strlen(elements[i]));
			cb_str_free(elements[i]);
			ptr += cb_strlen(elements[i]);
			if (i + 1 < array_len) {
				*ptr++ = ',';
				*ptr++ = ' ';
			}
		}
		*ptr++ = ']';
		assert(ptr == cb_strptr(&buf) + len);
		repr_leave(&val.val.as_array->gc_header);
		break;
	}

	case CB_VALUE_STRUCT: {
		char *ptr;
		struct cb_struct *s = val.val.as_struct;
		size_t struct_len = s->spec->nfields;
		cb_str *elements = alloca(struct_len * sizeof(cb_str));
		const char *name;
		size_t name_len;
		cb_str n = cb_agent_get_string(s->spec->name);
		name = cb_strptr(&n);
		name_len = cb_strlen(n);
		len = name_len + 2;

		if (repr_enter(&val.val.as_struct->gc_header)) {
			len += 3;  /* for "..." */
			cb_str_init(&buf, len);
			ptr = cb_strptr(&buf);
			memcpy(ptr, name, name_len);
			memcpy(ptr + name_len, "{...}", 5);
			break;
		}

		for (unsigned i = 0; i < struct_len; i += 1) {
			elements[i] = cb_value_to_string(s->fields[i]);
			len += cb_strlen(elements[i]);
			len += cb_strlen(cb_agent_get_string(
						s->spec->fields[i]));
			len += 3; /* space equal space */
			if (i != 0)
				len += 2;
		}

		cb_str_init(&buf, len);
		ptr = cb_strptr(&buf);
		memcpy(ptr, name, name_len);
		ptr += name_len;
		*ptr++ = '{';
		for (unsigned i = 0; i < struct_len; i += 1) {
			cb_str fname = cb_agent_get_string(
					s->spec->fields[i]);
			memcpy(ptr, cb_strptr(&fname), cb_strlen(fname));
			ptr += cb_strlen(fname);
			memcpy(ptr, " = ", 3);
			ptr += 3;
			memcpy(ptr, cb_strptr(&elements[i]),
					cb_strlen(elements[i]));
			cb_str_free(elements[i]);
			ptr += cb_strlen(elements[i]);
			if (i + 1 < struct_len) {
				*ptr++ = ',';
				*ptr++ = ' ';
			}
		}
		*ptr++ = '}';
		assert(ptr == cb_strptr(&buf) + len);
		repr_leave(&val.val.as_struct->gc_header);
		break;
	}

	case CB_VALUE_STRUCT_SPEC: {
		size_t name_id = val.val.as_struct_spec->name;
		char *ptr;
		const char *name;
		size_t name_len;
		cb_str n = cb_agent_get_string(name_id);
		name = cb_strptr(&n);
		name_len = cb_strlen(n);
		len = name_len + sizeof("<struct >") - 1;
		cb_str_init(&buf, len);
		ptr = cb_strptr(&buf);
		memcpy(ptr, "<struct ", 8);
		ptr += 8;
		memcpy(ptr, name, name_len);
		ptr += name_len;
		*ptr++ = '>';
		assert(ptr == cb_strptr(&buf) + len);
		break;
	}

	case CB_VALUE_USERDATA: {
		size_t size = sizeof("<userdata>") - 1;
		cb_str_init(&buf, size);
		memcpy(cb_strptr(&buf), "<userdata>", size);
		break;
	}

	default:
		fprintf(stderr, "unsupported to_string (%s)\n",
				cb_value_type_friendly_name(val.type));
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
		cb_str astr = cb_agent_get_string(a->val.as_interned_string),
		       bstr = b->val.as_string->string;
		if (cb_strlen(astr) != cb_strlen(bstr))
			return 0;
		return !memcmp(cb_strptr(&bstr), cb_strptr(&bstr),
				cb_strlen(astr));
	} else if (b->type == CB_VALUE_INTERNED_STRING
			&& a->type == CB_VALUE_STRING) {
		cb_str astr = cb_agent_get_string(b->val.as_interned_string),
		       bstr = a->val.as_string->string;
		if (cb_strlen(astr) != cb_strlen(bstr))
			return 0;
		return !memcmp(cb_strptr(&astr), cb_strptr(&bstr),
				cb_strlen(astr));
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
	case CB_VALUE_STRUCT_SPEC:
		return a->val.as_struct_spec == b->val.as_struct_spec;
	case CB_VALUE_NULL:
		return 1;
	case CB_VALUE_ARRAY: {
		if (a->val.as_array->len != b->val.as_array->len)
			return 0;
		struct cb_array *left, *right;
		left = a->val.as_array;
		right = b->val.as_array;
		for (unsigned i = 0; i < left->len; i += 1) {
			if (!cb_value_eq(&left->values[i], &right->values[i]))
				return 0;
		}
		return 1;
	}
	case CB_VALUE_STRING:
		return !cb_strcmp(a->val.as_string->string,
				b->val.as_string->string);
	case CB_VALUE_BYTES: {
		struct cb_bytes *bytes_a, *bytes_b;
		bytes_a = a->val.as_bytes;
		bytes_b = b->val.as_bytes;
		return 0 == cb_bytes_cmp(bytes_a, bytes_b);
	}
	case CB_VALUE_FUNCTION:
		if (a->val.as_function->type != b->val.as_function->type)
			return 0;
		if (a->val.as_function->type == CB_FUNCTION_USER)
			return a->val.as_function == b->val.as_function;
		return a->val.as_function->value.as_native
			== b->val.as_function->value.as_native;
	case CB_VALUE_INTERNED_STRING:
		return a->val.as_interned_string == b->val.as_interned_string;
	case CB_VALUE_STRUCT: {
		if (a->val.as_struct->spec != b->val.as_struct->spec)
			return 0;
		struct cb_struct *left, *right;
		left = a->val.as_struct;
		right = b->val.as_struct;
		for (unsigned i = 0; i < left->spec->nfields; i += 1) {
			if (!cb_value_eq(&left->fields[i], &right->fields[i]))
				return 0;
		}
		return 1;
	}
	case CB_VALUE_USERDATA:
		return a->val.as_userdata == b->val.as_userdata;
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
double cb_value_cmp(struct cb_value *a, struct cb_value *b, int *ok)
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
	case CB_VALUE_STRING:
		if (b->type == CB_VALUE_STRING) {
			return OK(cb_strcmp(a->val.as_string->string,
					b->val.as_string->string));
		} else if (b->type == CB_VALUE_INTERNED_STRING) {
			cb_str bstr = cb_agent_get_string(
					b->val.as_interned_string);
			return OK(cb_strcmp(a->val.as_string->string, bstr));
		} else {
			return UNDEFINED();
		}
	case CB_VALUE_INTERNED_STRING: {
		cb_str astr = cb_agent_get_string(a->val.as_interned_string);
		if (b->type == CB_VALUE_STRING) {
			return OK(cb_strcmp(astr, b->val.as_string->string));
		} else if (b->type == CB_VALUE_INTERNED_STRING) {
			cb_str bstr = cb_agent_get_string(
					b->val.as_interned_string);
			return OK(cb_strcmp(astr, bstr));
		} else {
			return UNDEFINED();
		}
	}

	default:
		return UNDEFINED();
	}

#undef UNDEFINED
#undef OK
}

void cb_function_add_upvalue(struct cb_user_function *fn,
		size_t idx, struct cb_upvalue *uv)
{
	uv->refcount += 1;
	fn->upvalues[idx] = uv;
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
	case CB_VALUE_BYTES:
		return "bytes";
	case CB_VALUE_CHAR:
		return "char";
	case CB_VALUE_ARRAY:
		return "array";
	case CB_VALUE_FUNCTION:
		return "function";
	case CB_VALUE_STRUCT:
		return "struct";
	case CB_VALUE_STRUCT_SPEC:
		return "struct spec";
	case CB_VALUE_USERDATA:
		return "userdata";
	}

	return "";
}

CB_INLINE const char *cb_value_type_of(struct cb_value *val)
{
	return cb_value_type_friendly_name(val->type);
}

int cb_value_call(struct cb_value fn, struct cb_value *args, size_t args_len,
		struct cb_value *result)
{
	if (fn.type != CB_VALUE_FUNCTION) {
		fprintf(stderr, "Value of type %s is not callable\n",
				cb_value_type_of(&fn));
		return 1;
	}

	return cb_vm_call(fn, args, args_len, result);
}

CB_INLINE cb_gc_header *cb_value_gc_header(const struct cb_value val)
{
	switch (val.type) {
	case CB_VALUE_INT:
	case CB_VALUE_DOUBLE:
	case CB_VALUE_BOOL:
	case CB_VALUE_CHAR:
	case CB_VALUE_NULL:
	case CB_VALUE_INTERNED_STRING:
		return NULL;

	case CB_VALUE_USERDATA:
		return &val.val.as_userdata->gc_header;

	case CB_VALUE_STRING:
		return &val.val.as_string->gc_header;

	case CB_VALUE_BYTES:
		return &val.val.as_bytes->gc_header;

	case CB_VALUE_ARRAY:
		return &val.val.as_array->gc_header;

	case CB_VALUE_FUNCTION:
		return &val.val.as_function->gc_header;

	case CB_VALUE_STRUCT:
		return &val.val.as_struct->gc_header;

	case CB_VALUE_STRUCT_SPEC:
		return &val.val.as_struct_spec->gc_header;
	}

	fprintf(stderr, "Unknown value type %d\n", val.type);
	abort();
}

static void value_mark_fn(void *obj)
{
	cb_value_mark(*(struct cb_value *) obj);
}

cb_gc_hold_key *cb_value_gc_hold(struct cb_value *val)
{
	return cb_gc_hold((void *) val, value_mark_fn);
}

static void queue_mark(struct cb_value *val)
{
	cb_gc_queue_mark((void *) val, value_mark_fn);
}

static void array_mark(struct cb_array *arr)
{
	for (unsigned i = 0; i < arr->len; i += 1)
		queue_mark(&arr->values[i]);
}

static void array_mark_fn(void *obj)
{
	array_mark((struct cb_array *) obj);
}

cb_gc_hold_key *cb_array_gc_hold(struct cb_array *arr)
{
	return cb_gc_hold((void *) arr, array_mark_fn);
}

void cb_value_mark(struct cb_value val)
{
#define GC_LOG(F) ({ \
		if (cb_options.debug_gc) { \
			cb_str as_str = cb_value_to_string(val); \
			printf("GC: marked value at %p: %s\n", \
					(cb_gc_header *) (F), \
					cb_strptr(&as_str)); \
			cb_str_free(as_str); \
		} \
	})

	cb_gc_header *header = cb_value_gc_header(val);
	if (!header || cb_gc_is_marked(header))
		return;

	cb_gc_mark(header);

	switch (val.type) {
	case CB_VALUE_INT:
	case CB_VALUE_DOUBLE:
	case CB_VALUE_BOOL:
	case CB_VALUE_CHAR:
	case CB_VALUE_NULL:
	case CB_VALUE_INTERNED_STRING:
		return;

	case CB_VALUE_USERDATA:
		GC_LOG(val.val.as_userdata);
		break;

	case CB_VALUE_STRING:
		GC_LOG(val.val.as_string);
		break;

	case CB_VALUE_BYTES:
		GC_LOG(val.val.as_bytes);
		break;

	case CB_VALUE_ARRAY: {
		GC_LOG(val.val.as_array);
		array_mark(val.val.as_array);
		break;
	}

	case CB_VALUE_FUNCTION: {
		struct cb_function *fn;
		struct cb_upvalue *uv;

		fn = val.val.as_function;
		GC_LOG(fn);
		if (fn->type == CB_FUNCTION_USER) {
			struct cb_user_function *ufunc = &fn->value.as_user;
			for (unsigned i = 0;
					i < ufunc->code->nupvalues;
					i += 1) {
				uv = ufunc->upvalues[i];
				if (uv->is_closed)
					queue_mark(&uv->v.value);
			}

			cb_code_mark(ufunc->code);
		}
		break;
	}

	case CB_VALUE_STRUCT: {
		GC_LOG(val.val.as_struct);
		cb_gc_mark(&val.val.as_struct->spec->gc_header);
		for (unsigned i = 0; i < val.val.as_struct->spec->nfields;
				i += 1)
			queue_mark(&val.val.as_struct->fields[i]);
		break;
	}

	case CB_VALUE_STRUCT_SPEC:
		GC_LOG(val.val.as_struct_spec);
		break;
	}
}

CB_INLINE struct cb_value cb_int(int64_t v)
{
	struct cb_value out;
	out.type = CB_VALUE_INT;
	out.val.as_int = v;
	return out;
}

CB_INLINE struct cb_value cb_double(double v)
{
	struct cb_value out;
	out.type = CB_VALUE_DOUBLE;
	out.val.as_double = v;
	return out;
}

CB_INLINE struct cb_value cb_bool(int v)
{
	struct cb_value out;
	out.type = CB_VALUE_BOOL;
	out.val.as_bool = v;
	return out;
}

CB_INLINE struct cb_value cb_char(uint32_t v)
{
	struct cb_value out;
	out.type = CB_VALUE_CHAR;
	out.val.as_char = v;
	return out;
}

struct cb_value cb_value_from_string(const char *str)
{
	struct cb_value retval;
	struct cb_string *sobj = cb_string_new();
	sobj->string = cb_str_from_cstr(str, strlen(str));
	retval.type = CB_VALUE_STRING;
	retval.val.as_string = sobj;
	return retval;
}

struct cb_value cb_value_from_fmt(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	size_t len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);
	char *str = malloc(sizeof(char) * len + 1);
	va_start(args, fmt);
	vsnprintf(str, len + 1, fmt, args);
	str[len] = 0;
	va_end(args);

	struct cb_value ret = cb_value_from_string(str);
	free(str);
	return ret;
}

struct cb_bytes *cb_bytes_new(size_t size)
{
	return cb_malloc(sizeof(struct cb_bytes) + size, NULL);
}

struct cb_value cb_bytes_new_value(size_t size)
{
	struct cb_value val;

	val.type = CB_VALUE_BYTES;
	val.val.as_bytes = cb_bytes_new(size);

	return val;
}