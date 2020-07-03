#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "agent.h"
#include "hashmap.h"
#include "intrinsics.h"
#include "value.h"

int println(size_t argc, struct cb_value *argv, struct cb_value *result);

void make_intrinsics(cb_hashmap *scope)
{
	struct cb_function *func;
	struct cb_value func_val;
	char *name;

	func = cb_function_new();
	func->type = CB_FUNCTION_NATIVE;
	func->value.as_native = println;

	func_val.type = CB_VALUE_FUNCTION;
	func_val.val.as_function = func;

	name = malloc(sizeof("println"));
	name[sizeof("println") - 1] = 0;
	memcpy(name, "println", sizeof("println") - 1);

	cb_hashmap_set(scope,
			cb_agent_intern_string(name, sizeof("println") - 1),
			func_val);
}

int println(size_t argc, struct cb_value *argv, struct cb_value *result)
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
	printf("\n");

	result->type = CB_VALUE_NULL;
	return 0;
}
