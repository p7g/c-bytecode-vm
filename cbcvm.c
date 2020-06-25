#include <stdio.h>
#include <stdlib.h>

#include "gc.h"
#include "value.h"

#include "compiler.c"

int main(void) {
	const char *text = (
		"function hello(a, b, c) {\n"
		"	return \"world\";\n"
		"}\n"
		"println(hello());\n"
	);
	struct token tok;
	lex_state state = NULL;

	while (!next_token(&state, text, &tok)) {
		size_t len = tok.end - tok.start;
		char *substr = malloc(len + 1);
		substr[len] = 0;
		for (int i = tok.start; i < tok.end; i += 1)
			substr[i - tok.start] = text[i];
		printf("%s: '%s' at %ld:%ld\n", token_type_name(tok.type),
				substr, tok.line, tok.column);
		free(substr);
	}

	printf("Done\n");
	return 0;
}
