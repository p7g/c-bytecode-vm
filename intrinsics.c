#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "utf8proc/utf8proc.h"

#include "agent.h"
#include "bytes.h"
#include "disassemble.h"
#include "error.h"
#include "eval.h"
#include "gc.h"
#include "hashmap.h"
#include "intrinsics.h"
#include "value.h"
#include "userdata.h"

#define FUNC(NAME, FN, ARITY) ({ \
		struct cb_value _func_val; \
		size_t _name; \
		_name = cb_agent_intern_string(NAME, sizeof(NAME) - 1); \
		_func_val = cb_cfunc_new(_name, (ARITY), \
				(cb_native_function *) (FN)); \
		cb_hashmap_set(scope, _name, _func_val); \
	});

#define DECL(NAME, FN, _ARITY) static int FN(size_t, struct cb_value *, \
		struct cb_value *);

#define INTRINSIC_LIST(X) \
	X("print", print, 0) \
	X("println", println, 0) \
	X("tostring", tostring, 1) \
	X("typeof", typeof_, 1) \
	X("string_chars", string_chars, 1) \
	X("string_from_chars", string_from_chars, 1) \
	X("string_bytes", string_bytes, 1) \
	X("string_concat", string_concat, 0) \
	X("ord", ord, 1) \
	X("chr", chr, 1) \
	X("truncate32", truncate32, 1) \
	X("tofloat", tofloat, 1) \
	X("read_file", read_file, 1) \
	X("argv", argv, 0) \
	X("__upvalues", upvalues, 1) \
	X("apply", apply, 2) \
	X("now", now, 0) \
	X("read_file_bytes", read_file_bytes, 1) \
	X("toint", toint, 1) \
	X("__gc_collect", __gc_collect, 0) \
	X("__dis", __dis, 1) \
	X("arguments", arguments, 0) \
	X("id", id, 1)

INTRINSIC_LIST(DECL);

void make_intrinsics(cb_hashmap *scope)
{
	INTRINSIC_LIST(FUNC);
}

static int print(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	int first;
	cb_str as_string;

	first = 1;
	for (unsigned i = 0; i < argc; i += 1) {
		as_string = cb_value_to_string(argv[i]);
		printf("%s%s", first ? "" : " ", cb_strptr(&as_string));
		if (first)
			first = 0;
		cb_str_free(as_string);
	}

	/* hack to flush only if this isn't called by println */
	if (result != NULL) {
		fflush(stdout);
		result->type = CB_VALUE_NULL;
	}

	return 0;
}

static int println(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	print(argc, argv, NULL);
	putchar('\n');

	result->type = CB_VALUE_NULL;
	return 0;
}

static int tostring(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str as_string;

	as_string = cb_value_to_string(argv[0]);

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	result->val.as_string->string = as_string;

	return 0;
}

static int typeof_(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	const char *type;

	type = cb_value_type_of(&argv[0]);
	*result = (struct cb_value) {
		.type = CB_VALUE_STRING,
		.val.as_string = cb_string_new(),
	};

	ssize_t strvalid = cb_str_from_cstr(type, strlen(type),
			&result->val.as_string->string);
	if (strvalid < 0) {
		struct cb_value err;
		cb_value_from_string(&err, cb_str_errmsg(strvalid));
		cb_error_set(err);
		return 1;
	}

	return 0;
}

static int string_chars(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str str;
	size_t pos, ncodepoints, len;

	str = CB_EXPECT_STRING(argv[0]);

	len = cb_strlen(str);
	ncodepoints = cb_str_ncodepoints(str);
	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new(ncodepoints);

	pos = 0;
	for (unsigned i = 0; i < ncodepoints; i += 1) {
		struct cb_value *val = &result->val.as_array->values[i];
		val->type = CB_VALUE_CHAR;

		ssize_t result = cb_str_read_char(str, pos, &val->val.as_char);
		if (result < 0) {
			struct cb_value err;
			cb_value_from_string(&err, cb_str_errmsg(result));
			cb_error_set(err);
			return 1;
		}
		pos += result;
	}

	assert(pos == len);
	return 0;
}

static int string_from_chars(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value arr;
	unsigned char *str;
	size_t pos, len;

	arr = argv[0];
	CB_EXPECT_TYPE(CB_VALUE_ARRAY, arr);

	len = arr.val.as_array->len;
	str = malloc(len * 4 + 1);
	pos = 0;

	for (unsigned i = 0; i < len; i += 1) {
		struct cb_value charval = arr.val.as_array->values[i];
		if (charval.type != CB_VALUE_CHAR) {
			struct cb_value err;
			cb_value_from_string(&err,
					"string_from_chars: Expected array of chars\n");
			cb_error_set(err);
			free(str);
			return 1;
		}
		pos += utf8proc_encode_char(charval.val.as_char, str + pos);
	}
	str[pos] = 0;

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	ssize_t strvalid = cb_str_take_cstr((char *) str, len,
			&result->val.as_string->string);
	if (strvalid < 0) {
		struct cb_value err;
		cb_value_from_string(&err, cb_str_errmsg(strvalid));
		cb_error_set(err);
		return 1;
	}

	return 0;
}

static int string_bytes(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str str;
	size_t len;

	str = CB_EXPECT_STRING(argv[0]);

	len = cb_strlen(str);
	*result = cb_bytes_new_value(len);

	memcpy(cb_bytes_ptr(result->val.as_bytes), cb_strptr(&str), len);

	return 0;
}

static int string_concat(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	size_t len, offset;
	cb_str str;
	char *buf;

	len = 0;
	for (unsigned i = 0; i < argc; i += 1) {
		str = CB_EXPECT_STRING(argv[i]);
		len += cb_strlen(str);
	}

	offset = 0;
	buf = malloc(len + 1);

	for (unsigned i = 0; i < argc; i += 1) {
		str = CB_EXPECT_STRING(argv[i]);
		memcpy(buf + offset, cb_strptr(&str), cb_strlen(str));
		offset += cb_strlen(str);
	}

	buf[len] = 0;

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	ssize_t strvalid = cb_str_take_cstr(buf, len,
			&result->val.as_string->string);
	if (strvalid < 0) {
		struct cb_value err;
		cb_value_from_string(&err, cb_str_errmsg(strvalid));
		cb_error_set(err);
		return 1;
	}

	return 0;
}

static int ord(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_CHAR, argv[0]);

	result->type = CB_VALUE_INT;
	result->val.as_int = argv[0].val.as_char;

	return 0;
}

static int chr(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	int64_t intval;

	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);
	intval = argv[0].val.as_int;
	if (intval > INT32_MAX || intval < INT32_MIN
			|| !utf8proc_codepoint_valid(intval)) {
		struct cb_value err;
		cb_value_from_fmt(&err, "Invalid codepoint");
		cb_error_set(err);
		return 1;
	}

	result->type = CB_VALUE_CHAR;
	result->val.as_char = argv[0].val.as_int;

	return 0;
}

static int truncate32(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);

	*result = argv[0];
	result->val.as_int = result->val.as_int & 0xFFFFFFFF;

	return 0;
}

static int tofloat(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	CB_EXPECT_TYPE(CB_VALUE_INT, argv[0]);

	result->type = CB_VALUE_DOUBLE;
	result->val.as_double = (double) argv[0].val.as_int;

	return 0;
}

static int read_file(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str str;
	FILE *f;
	size_t len;
	char *buf;

	str = CB_EXPECT_STRING(argv[0]);

	f = fopen(cb_strptr(&str), "r");
	if (!f) {
		perror("read_file");
		return 1;
	}

#define X(EXPR) ({ \
		if (EXPR) { \
			perror("read_file"); \
			return 1; \
		} \
	})

	X(fseek(f, 0, SEEK_END));
	len = ftell(f);
	X(fseek(f, 0, SEEK_SET));

	buf = malloc(len + 1);
	buf[len] = 0;
	fread(buf, sizeof(char), len, f);
	fclose(f);

#undef X

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	ssize_t strvalid = cb_str_take_cstr(buf, len,
			&result->val.as_string->string);
	if (strvalid < 0) {
		struct cb_value err;
		cb_value_from_string(&err, cb_str_errmsg(strvalid));
		cb_error_set(err);
		return 1;
	}

	return 0;
}

static int read_file_bytes(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	cb_str str;
	FILE *f;
	size_t len;
	char *buf;

	str = CB_EXPECT_STRING(argv[0]);

	f = fopen(cb_strptr(&str), "rb");
	if (!f) {
		perror("fopen");
		return 1;
	}

#define X(EXPR) ({ \
		if (EXPR) { \
			perror("fopen"); \
			return 1; \
		} \
	})

	X(fseek(f, 0, SEEK_END));
	len = ftell(f);
	X(fseek(f, 0, SEEK_SET));

	buf = malloc(len + 1);
	fread(buf, sizeof(char), len, f);
	fclose(f);

#undef X

	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new(len);
	result->val.as_array->len = len;

	for (unsigned i = 0; i < len; i += 1) {
		result->val.as_array->values[i] = (struct cb_value) {
			.type = CB_VALUE_INT,
			.val.as_int = buf[i] & 0xFF,
		};
	}

	free(buf);
	return 0;
}

static int _argc;
static char **_argv;

void cb_intrinsics_set_argv(int argc, char **argv)
{
	_argc = argc;
	_argv = argv;
}

static int argv(size_t argc, struct cb_value *argv_, struct cb_value *result)
{
	struct cb_value *current;

	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new(_argc);
	result->val.as_array->len = _argc;

	for (int i = 0; i < _argc; i += 1) {
		current = &result->val.as_array->values[i];
		current->type = CB_VALUE_STRING;
		current->val.as_string = cb_string_new();
		ssize_t strvalid = cb_str_from_cstr(_argv[i], strlen(_argv[i]),
				&current->val.as_string->string);
		if (strvalid < 0) {
			struct cb_value err;
			cb_value_from_string(&err, cb_str_errmsg(strvalid));
			cb_error_set(err);
			return 1;
		}
	}

	return 0;
}

static int upvalues(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	struct cb_function *funcval;
	size_t upvalue_count;

	CB_EXPECT_TYPE(CB_VALUE_FUNCTION, argv[0]);

	funcval = argv[0].val.as_function;
	upvalue_count = cb_function_upvalue_count(funcval);
	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new(upvalue_count);

	for (unsigned i = 0; i < upvalue_count; i += 1) {
		struct cb_upvalue *uv = funcval->upvalues[i];
		struct cb_value v = cb_load_upvalue(uv);
		result->val.as_array->values[i] = v;
	}

	return 0;
}

static int apply(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	struct cb_array *arr;

	CB_EXPECT_TYPE(CB_VALUE_FUNCTION, argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_ARRAY, argv[1]);

	arr = argv[1].val.as_array;
	if (arr->len < argv[0].val.as_function->arity) {
		cb_str s = cb_agent_get_string(argv[0].val.as_function->name);
		struct cb_value err;
		cb_value_from_fmt(&err, "Too few arguments to function '%s'",
				cb_strptr(&s));
		cb_error_set(err);
		return 1;
	}

	return cb_value_call(argv[0], arr->values, arr->len, result);
}

static int now(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);

	result->type = CB_VALUE_DOUBLE;
	result->val.as_double = t.tv_sec + 1e-9 * t.tv_nsec;

	return 0;
}

static int toint(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	struct cb_value arg;

	arg = argv[0];
	result->type = CB_VALUE_INT;

	if (arg.type == CB_VALUE_INT)
		result->val.as_int = arg.val.as_int;
	else if (arg.type == CB_VALUE_DOUBLE)
		result->val.as_int = (intptr_t) arg.val.as_double;
	else if (arg.type == CB_VALUE_CHAR)
		result->val.as_int = (intptr_t) arg.val.as_char;
	else {
		struct cb_value err;
		cb_value_from_fmt(&err, "int: Cannot convert value of type %s to int",
				cb_value_type_friendly_name(arg.type));
		cb_error_set(err);
		return 1;
	}

	return 0;
}

static int __gc_collect(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	result->type = CB_VALUE_NULL;
	cb_gc_collect();
	return 0;
}

static int __dis(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	struct cb_user_function *func;

	CB_EXPECT_TYPE(CB_VALUE_FUNCTION, argv[0]);
	if (argv[0].val.as_function->type != CB_FUNCTION_USER) {
		struct cb_value err;
		cb_value_from_string(&err,
				"__dis: Cannot disassemble native function");
		cb_error_set(err);
		return 1;
	}

	func = &argv[0].val.as_function->value.as_user;

	result->type = CB_VALUE_NULL;
	return cb_disassemble_recursive(func->code);
}

static int arguments(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_frame *frame = cb_vm_state.frame;
	assert(frame && frame->parent);
	frame = frame->parent;

	struct cb_array *args = cb_array_new(frame->num_args);
	for (unsigned i = 0; i < frame->num_args; i++) {
		args->values[i] = cb_vm_state.stack[frame->bp + i + 1];
	}

	result->type = CB_VALUE_ARRAY;
	result->val.as_array = args;

	return 0;
}

static int id(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	result->type = CB_VALUE_INT;

	if (cb_value_id(argv[0], &result->val.as_int)) {
		struct cb_value err;
		cb_value_from_fmt(&err, "Value of type %s has no identity", cb_value_type_friendly_name(argv[0].type));
		cb_error_set(err);
		return 1;
	}

	return 0;
}