#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "agent.h"
#include "hashmap.h"
#include "intrinsics.h"
#include "value.h"

#define FUNC(NAME, ARITY, FN) ({ \
		struct cb_function *_func; \
		struct cb_value _func_val; \
		size_t _name; \
		_name = cb_agent_intern_string(strdup(NAME), sizeof(NAME) - 1); \
		_func = cb_function_new(); \
		_func->arity = (ARITY); \
		_func->name = _name; \
		_func->type = CB_FUNCTION_NATIVE; \
		_func->value.as_native = (cb_native_function *) (FN); \
		_func_val.type = CB_VALUE_FUNCTION; \
		_func_val.val.as_function = _func; \
		cb_hashmap_set(scope, _name, _func_val); \
	})

int print(size_t argc, struct cb_value *argv, struct cb_value *result);
int println(size_t argc, struct cb_value *argv, struct cb_value *result);

void make_intrinsics(cb_hashmap *scope)
{
	FUNC("println", 0, println);
	FUNC("print", 0, print);
}

int print(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	int i, first;
	char *as_string;

	first = 1;
	for (i = 0; i < argc; i += 1) {
		if (first)
			first = 0;
		as_string = cb_value_to_string(&argv[i]);
		printf("%s%s", first ? "" : " ", as_string);
		free(as_string);
	}

	result->type = CB_VALUE_NULL;
	return 0;
}

int println(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	print(argc, argv, result);
	printf("\n");

	return 0;
}
