/* Here lies the entire c-bytecode-vm compiler. This includes lexing, parsing,
 * and code generation. The latter two happen at the same time.
 */

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LENGTH(ARR) (sizeof(ARR) / sizeof(ARR[0]))

/* Parser position state */
static const char *current_file;

/* Lexer position state */
static int current_line;
static int current_column;

/* Abort with a formatted error message */
#define die(...) \
	do { \
		fprintf(stderr, __VA_ARGS__); \
		exit(1); \
	} while (0)

/* Abort with an error message and location data */
#define error(message, ...) \
	die("Error in %s at %d:%d: " message "\n", \
		current_file, current_line, current_column, ##__VA_ARGS__)

#define TOKEN_TYPE_LIST(X) \
	X(TOK_IDENT) \
	X(TOK_INT) \
	X(TOK_DOUBLE) \
	X(TOK_STRING) \
	X(TOK_CHAR) \
	X(TOK_NULL) \
	X(TOK_LEFT_BRACKET) \
	X(TOK_RIGHT_BRACKET) \
	X(TOK_LEFT_BRACE) \
	X(TOK_RIGHT_BRACE) \
	X(TOK_LEFT_PAREN) \
	X(TOK_RIGHT_PAREN) \
	X(TOK_PLUS) \
	X(TOK_MINUS) \
	X(TOK_STAR) \
	X(TOK_SLASH) \
	X(TOK_PERCENT) \
	X(TOK_STAR_STAR) \
	X(TOK_AND) \
	X(TOK_PIPE) \
	X(TOK_CARET) \
	X(TOK_AND_AND) \
	X(TOK_PIPE_PIPE) \
	X(TOK_SEMICOLON) \
	X(TOK_EQUAL) \
	X(TOK_EQUAL_EQUAL) \
	X(TOK_TILDE) \
	X(TOK_BANG) \
	X(TOK_BANG_EQUAL) \
	X(TOK_LESS_THAN) \
	X(TOK_LESS_THAN_EQUAL) \
	X(TOK_GREATER_THAN) \
	X(TOK_GREATER_THAN_EQUAL) \
	X(TOK_COMMA) \
	X(TOK_DOT) \
	X(TOK_RETURN) \
	X(TOK_FUNCTION) \
	X(TOK_FOR) \
	X(TOK_IF) \
	X(TOK_ELSE) \
	X(TOK_WHILE) \
	X(TOK_BREAK) \
	X(TOK_CONTINUE) \
	X(TOK_LET) \
	X(TOK_TRUE) \
	X(TOK_FALSE) \
	X(TOK_MODULE) \
	X(TOK_EXPORT) \
	X(TOK_IMPORT)

enum token_type {
#define COMMA(V) V,
	TOKEN_TYPE_LIST(COMMA)
#undef COMMA
};

/* A structure defining a token located in a source file */
struct token {
	/* The type of token */
	enum token_type type;
	/* The range of source text where the token was found.
	 * start is inclusive, end is exclusive. */
	size_t start, end;
};

/* Opaque lexer state */
typedef const char *lex_state;

static struct {
	const char *name;
	enum token_type type;
} KEYWORDS[] = {
	{"return", TOK_RETURN},
	{"for", TOK_FOR},
	{"while", TOK_WHILE},
	{"function", TOK_FUNCTION},
	{"let", TOK_LET},
	{"if", TOK_IF},
	{"else", TOK_ELSE},
	{"break", TOK_BREAK},
	{"continue", TOK_CONTINUE},
	{"null", TOK_NULL},
	{"true", TOK_TRUE},
	{"false", TOK_FALSE},
	{"module", TOK_MODULE},
	{"export", TOK_EXPORT},
	{"import", TOK_IMPORT},
};

/* Read the next token from the given input string */
int next_token(lex_state *state, const char *input, struct token *dest)
{
	const char *error_message, *start_of_token;
	int is_double, c, len, i;

	assert(state != NULL && input != NULL && dest != NULL);
	if (*state == NULL) {
		current_line = current_column = 1;
		*state = input;
	}

#define NEXT() (current_column += 1, *(*state)++)
#define PEEK() (**state)
#define TOKEN(TYPE) ({ \
		dest->type = (TYPE); \
		goto set_dest; \
	})
#define OR2(SND, A, B) ({ \
		if (PEEK() == (SND)) { \
			(void) NEXT(); \
			TOKEN(B); \
		} else { \
			TOKEN(A); \
		} \
	})

	while (PEEK()) {
		start_of_token = *state;

		switch (NEXT()) {
		/* Skip whitespace, but count newlines */
		case '\n':
			current_line += 1;
			current_column = 1;
			continue;
		case ' ':
		case '\t':
		case '\r':
			continue;

		/* Skip comments */
		case '#':
			while (PEEK() != '\n') NEXT();
			continue;

		case '+': TOKEN(TOK_PLUS);
		case '-': TOKEN(TOK_MINUS);
		case '/': TOKEN(TOK_SLASH);
		case '^': TOKEN(TOK_CARET);
		case '[': TOKEN(TOK_LEFT_BRACKET);
		case ']': TOKEN(TOK_RIGHT_BRACKET);
		case '{': TOKEN(TOK_LEFT_BRACE);
		case '}': TOKEN(TOK_RIGHT_BRACE);
		case '(': TOKEN(TOK_LEFT_PAREN);
		case ')': TOKEN(TOK_RIGHT_PAREN);
		case '%': TOKEN(TOK_PERCENT);
		case ';': TOKEN(TOK_SEMICOLON);
		case ',': TOKEN(TOK_COMMA);
		case '~': TOKEN(TOK_TILDE);
		case '.': TOKEN(TOK_DOT);
		case '*': OR2('*', TOK_STAR, TOK_STAR_STAR);
		case '&': OR2('&', TOK_AND, TOK_AND_AND);
		case '|': OR2('|', TOK_PIPE, TOK_PIPE_PIPE);
		case '<': OR2('=', TOK_LESS_THAN, TOK_LESS_THAN_EQUAL);
		case '>': OR2('=', TOK_GREATER_THAN, TOK_GREATER_THAN_EQUAL);
		case '!': OR2('=', TOK_BANG, TOK_BANG_EQUAL);

		case '0' ... '9':
			is_double = 0;
			while ((c = PEEK())) {
				if (c == '.' && !is_double) {
					is_double = 1;
				} else if (c == '.' && is_double) {
					error("Unexpected '.'");
				} else if (!isdigit(c)) {
					break;
				}
				(void) NEXT();
			}
			if (is_double)
				TOKEN(TOK_DOUBLE);
			else
				TOKEN(TOK_INT);

		case '_':
		case 'a' ... 'z':
		case 'A' ... 'Z':
			len = 0;
			while ((c = PEEK())) {
				if (isalnum(c) || c == '_');
				else break;
				(void) NEXT();
				len += 1;
			}

			/* Check if it was a keyword */
			for (i = 0; i < LENGTH(KEYWORDS); i += 1) {
				if (!strncmp(start_of_token,
							KEYWORDS[i].name,
							len))
					TOKEN(KEYWORDS[i].type);
			}
			TOKEN(TOK_IDENT);
		}
	}

	/* end of input */
	return -1;

#undef NEXT
#undef PEEK
#undef TOKEN
#undef OR2

set_dest:
	dest->end = *state - input;
	dest->start = start_of_token - input;
	return 0;
}

/* Get the name of a token type given its value */
const char *token_type_name(enum token_type t)
{
#define CASE(T) case T: return #T;

	switch (t) {
	TOKEN_TYPE_LIST(CASE)
	default:
		return NULL;
	}

#undef CASE
}
