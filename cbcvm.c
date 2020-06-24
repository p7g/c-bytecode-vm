#include <stdio.h>
#include <stdlib.h>

#include "gc.h"
#include "value.h"

#include "compiler.c"

int main(void) {
	const char *text = (
		"1.2 123 "
	);
	struct token tok;
	lex_state state = NULL;

	while (!next_token(&state, text, &tok)) {
		size_t len = tok.end - tok.start;
		char *substr = malloc(len + 1);
		substr[len] = 0;
		for (int i = tok.start; i < tok.end; i += 1)
			substr[i - tok.start] = text[i];
		printf("TOKEN %s: '%s' at %d:%d\n", token_type_name(tok.type),
				substr, current_line, current_column);
		free(substr);
	}

	printf("Done\n");
	return 0;
}
