#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "agent.h"
#include "eval.h"
#include "hashmap.h"
#include "intrinsics.h"
#include "value.h"

#define FUNC(FN, ARITY) ({ \
		struct cb_function *_func; \
		struct cb_value _func_val; \
		size_t _name; \
		_name = cb_agent_intern_string(strdup(#FN), sizeof(#FN) - 1); \
		_func = cb_function_new(); \
		_func->arity = (ARITY); \
		_func->name = _name; \
		_func->type = CB_FUNCTION_NATIVE; \
		_func->value.as_native = (cb_native_function *) (FN); \
		_func_val.type = CB_VALUE_FUNCTION; \
		_func_val.val.as_function = _func; \
		cb_hashmap_set(scope, _name, _func_val); \
	});

#define DECL(NAME, _ARITY) static int NAME(size_t, struct cb_value *, \
		struct cb_value *);

#define INTRINSIC_LIST(X) \
	X(print, 0) \
	X(println, 0) \
	X(tostring, 1) \
	X(type_of, 1) \
	X(array_new, 1) \
	X(string_chars, 1) \
	X(string_from_chars, 1) \
	X(string_bytes, 1) \
	X(string_concat, 0) \
	X(ord, 1) \
	X(chr, 1) \
	X(array_length, 1) \
	X(truncate32, 1) \
	X(tofloat, 1) \
	X(read_file, 1) \
	X(argv, 0) \
	X(upvalues, 0)

INTRINSIC_LIST(DECL);

void make_intrinsics(cb_hashmap *scope)
{
	INTRINSIC_LIST(FUNC);
}

#define EXPECT_TYPE(NAME, TYPE, VAL) ({ \
		struct cb_value _val = (VAL); \
		if (_val.type != (TYPE)) { \
			fprintf(stderr, NAME \
					": expected %s argument, got %s\n", \
					cb_value_type_friendly_name(TYPE), \
					cb_value_type_of(&_val)); \
			return 1; \
		} \
	})

static int print(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	int i, first;
	char *as_string;

	first = 1;
	for (i = 0; i < argc; i += 1) {
		as_string = cb_value_to_string(&argv[i]);
		printf("%s%s", first ? "" : " ", as_string);
		if (first)
			first = 0;
		free(as_string);
	}

	/* hack to flush only if this isn't called by println */
	if (result != NULL)
		fflush(stdout);

	result->type = CB_VALUE_NULL;
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
	char *as_string;

	as_string = cb_value_to_string(&argv[0]);

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	result->val.as_string->string.chars = as_string;
	result->val.as_string->string.len = strlen(as_string);

	return 0;
}

static int type_of(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	const char *type;

	type = cb_value_type_of(&argv[0]);
	*result = (struct cb_value) {
		.type = CB_VALUE_STRING,
		.val.as_string = cb_string_new(),
	};

	result->val.as_string->string = (struct cb_str) {
		.chars = strdup(type),
		.len = strlen(type),
	};

	return 0;
}

static int array_new(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value len_val;
	size_t len;
	len_val = argv[0];

	EXPECT_TYPE("array_new", CB_VALUE_INT, len_val);
	len = len_val.val.as_int;

	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new(len);
	result->val.as_array->len = len;

	for (int i = 0; i < len; i += 1)
		result->val.as_array->values[i].type = CB_VALUE_NULL;

	return 0;
}

static int string_chars(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value str_val;
	cb_str str;
	size_t len;

	str_val = argv[0];
	if (str_val.type == CB_VALUE_STRING) {
		str = str_val.val.as_string->string;
	} else if (str_val.type == CB_VALUE_INTERNED_STRING) {
		str = cb_agent_get_string(str_val.val.as_interned_string);
	} else {
		EXPECT_TYPE("string_chars", CB_VALUE_STRING, str_val);
		return 1;
	}

	len = cb_strlen(str);
	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new(len);
	result->val.as_array->len = len;

	for (int i = 0; i < len; i += 1) {
		result->val.as_array->values[i] = (struct cb_value) {
			.type = CB_VALUE_CHAR,
			.val.as_char = cb_str_at(str, i),
		};
	}

	return 0;
}

static int string_from_chars(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value arr;
	char *str;
	size_t len;

	arr = argv[0];
	EXPECT_TYPE("string_from_chars", CB_VALUE_ARRAY, arr);

	len = arr.val.as_array->len;
	str = malloc(len + 1);

	for (int i = 0; i < len; i += 1) {
		if (arr.val.as_array->values[i].type != CB_VALUE_CHAR) {
			fprintf(stderr, "string_from_chars: Expected array of chars\n");
			free(str);
			return 1;
		}
		/* FIXME: unicode */
		str[i] = arr.val.as_array->values[i].val.as_char & 0xFF;
	}
	str[len] = 0;

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	result->val.as_string->string.len = len;
	result->val.as_string->string.chars = str;

	return 0;
}

static int string_bytes(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value str_val;
	cb_str str;
	size_t len;

	str_val = argv[0];
	if (str_val.type == CB_VALUE_STRING) {
		str = str_val.val.as_string->string;
	} else if (str_val.type == CB_VALUE_INTERNED_STRING) {
		str = cb_agent_get_string(str_val.val.as_interned_string);
	} else {
		EXPECT_TYPE("string_bytes", CB_VALUE_STRING, str_val);
		return 1;
	}

	len = cb_strlen(str);
	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new(len);
	result->val.as_array->len = len;

	for (int i = 0; i < len; i += 1) {
		result->val.as_array->values[i] = (struct cb_value) {
			.type = CB_VALUE_INT,
			.val.as_int = cb_str_at(str, i),
		};
	}

	return 0;
}

static int string_concat(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	size_t len, offset;
	cb_str str;
	char *buf;

	len = 0;
	for (int i = 0; i < argc; i += 1) {
		if (argv[i].type == CB_VALUE_STRING) {
			str = argv[i].val.as_string->string;
		} else if (argv[i].type == CB_VALUE_INTERNED_STRING) {
			str = cb_agent_get_string(
					argv[i].val.as_interned_string);
		} else {
			EXPECT_TYPE("string_concat", CB_VALUE_STRING, argv[i]);
		}

		len += cb_strlen(str);
	}

	offset = 0;
	buf = malloc(len + 1);

	for (int i = 0; i < argc; i += 1) {
		if (argv[i].type == CB_VALUE_STRING) {
			str = argv[i].val.as_string->string;
		} else if (argv[i].type == CB_VALUE_INTERNED_STRING) {
			str = cb_agent_get_string(
					argv[i].val.as_interned_string);
		}
		memcpy(buf + offset, cb_strptr(str), cb_strlen(str));
		offset += cb_strlen(str);
	}

	buf[len] = 0;

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	result->val.as_string->string = (struct cb_str) {
		.len = len,
		.chars = buf,
	};

	return 0;
}

static int ord(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	EXPECT_TYPE("ord", CB_VALUE_CHAR, argv[0]);

	result->type = CB_VALUE_INT;
	result->val.as_int = argv[0].val.as_char;

	return 0;
}

static int chr(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	EXPECT_TYPE("chr", CB_VALUE_INT, argv[0]);

	result->type = CB_VALUE_CHAR;
	result->val.as_char = argv[0].val.as_int & 0xFF;

	return 0;
}

static int array_length(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	EXPECT_TYPE("array_length", CB_VALUE_ARRAY, argv[0]);

	result->type = CB_VALUE_INT;
	result->val.as_int = argv[0].val.as_array->len;

	return 0;
}

static int truncate32(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	EXPECT_TYPE("truncate32", CB_VALUE_INT, argv[0]);

	*result = argv[0];
	result->val.as_int = result->val.as_int & 0xFFFFFFFF;

	return 0;
}

static int tofloat(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	EXPECT_TYPE("tofloat", CB_VALUE_INT, argv[0]);

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

	if (argv[0].type == CB_VALUE_STRING) {
		str = argv[0].val.as_string->string;
	} else if (argv[0].type == CB_VALUE_INTERNED_STRING) {
		str = cb_agent_get_string(argv[0].val.as_interned_string);
	} else {
		EXPECT_TYPE("read_file", CB_VALUE_STRING, argv[0]);
		return 1;
	}

	f = fopen(cb_strptr(str), "r");
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
	fread(buf, sizeof(char), len, f);

#undef X

	result->type = CB_VALUE_STRING;
	result->val.as_string = cb_string_new();
	result->val.as_string->string = (struct cb_str) {
		.len = len,
		.chars = buf,
	};

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
		current->val.as_string->string = (struct cb_str) {
			.len = strlen(_argv[i]),
			.chars = strdup(_argv[i]),
		};
	}

	return 0;
}

static int upvalues(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	struct cb_user_function *caller;
	int i;

	caller = cb_caller();
	result->type = CB_VALUE_ARRAY;
	result->val.as_array = cb_array_new(caller->upvalues_len);
	result->val.as_array->len = caller->upvalues_len;

	for (i = 0; i < caller->upvalues_len; i += 1)
		result->val.as_array->values[i] = cb_get_upvalue(
				caller->upvalues[i]);

	return 0;
}
