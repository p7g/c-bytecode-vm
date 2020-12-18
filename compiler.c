/* Here lies the entire c-bytecode-vm compiler. This includes lexing, parsing,
 * and code generation. The latter two happen at the same time. */

#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "builtin_modules.h"
#include "compiler.h"
#include "hashmap.h"
#include "module.h"
#include "opcode.h"
#include "struct.h"

#define INITIAL_BYTECODE_SIZE 32
#define LENGTH(ARR) (sizeof(ARR) / sizeof(ARR[0]))
#define VALID_ESCAPES "nrt\"'"

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
	X(TOK_COLON) \
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
	X(TOK_EXPORT) \
	X(TOK_IMPORT) \
	X(TOK_STRUCT) \
	X(TOK_EOF)

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
	/* The line and column where the token started, mostly for displaying
	 * to the user */
	size_t line, column;
};

/* Opaque lexer state */
typedef struct lex_state {
	const char *filename;
	size_t current_line, current_column;
	const char *ptr;
} lex_state;

static inline lex_state lex_state_new(const char *name)
{
	return (lex_state) {
		.filename = name,
		.current_line = 1,
		.current_column = 1,
	};
}

#define KW(S, T) { S, sizeof(S) - 1, T }
static struct {
	const char *name;
	size_t len;
	enum token_type type;
} KEYWORDS[] = {
	KW("return", TOK_RETURN),
	KW("for", TOK_FOR),
	KW("while", TOK_WHILE),
	KW("function", TOK_FUNCTION),
	KW("let", TOK_LET),
	KW("if", TOK_IF),
	KW("else", TOK_ELSE),
	KW("break", TOK_BREAK),
	KW("continue", TOK_CONTINUE),
	KW("null", TOK_NULL),
	KW("true", TOK_TRUE),
	KW("false", TOK_FALSE),
	KW("export", TOK_EXPORT),
	KW("import", TOK_IMPORT),
	KW("struct", TOK_STRUCT),
};
#undef KW

/* Get the name of a token type given its value */
static const char *token_type_name(enum token_type t)
{
#define CASE(T) case T: return #T;

	switch (t) {
	TOKEN_TYPE_LIST(CASE)
	default:
		return NULL;
	}

#undef CASE
}

/* Read the next token from the given input string */
static int next_token(lex_state *state, const char *input, struct token *dest)
{
	const char *start_of_token;
	int is_double, c, len, i;

	assert(state != NULL);
	assert(input != NULL);
	assert(dest != NULL);
	if (state->ptr == NULL) {
		state->current_line = state->current_column = 1;
		state->ptr = input;
	}

/* Print an error message and location data */
#define ERROR(message, ...) \
	fprintf(stderr, "Error in %s at %zu:%zu: " message "\n", \
		state->filename, state->current_line, state->current_column, \
		##__VA_ARGS__)
#define NEXT() (state->current_column += 1, *state->ptr++)
#define PEEK() (*state->ptr)
#define TOKEN(TYPE) ({ dest->type = (TYPE); goto end; })
#define OR2(SND, A, B) ({ \
		if (PEEK() == (SND)) { \
			(void) NEXT(); \
			TOKEN(B); \
		} else { \
			TOKEN(A); \
		} \
	})

	start_of_token = state->ptr;

	while (PEEK()) {
		start_of_token = state->ptr;
		dest->line = state->current_line;
		dest->column = state->current_column;

		switch ((c = NEXT())) {
		/* Skip whitespace, but count newlines */
		case '\n':
			state->current_line += 1;
			state->current_column = 1;
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
		case ':': TOKEN(TOK_COLON);
		case ',': TOKEN(TOK_COMMA);
		case '~': TOKEN(TOK_TILDE);
		case '.': TOKEN(TOK_DOT);
		case '=': OR2('=', TOK_EQUAL, TOK_EQUAL_EQUAL);
		case '*': OR2('*', TOK_STAR, TOK_STAR_STAR);
		case '&': OR2('&', TOK_AND, TOK_AND_AND);
		case '|': OR2('|', TOK_PIPE, TOK_PIPE_PIPE);
		case '<': OR2('=', TOK_LESS_THAN, TOK_LESS_THAN_EQUAL);
		case '>': OR2('=', TOK_GREATER_THAN, TOK_GREATER_THAN_EQUAL);
		case '!': OR2('=', TOK_BANG, TOK_BANG_EQUAL);

		case '0' ... '9':
			if (PEEK() == 'x') {
				NEXT();
				while ((c = PEEK()) && isxdigit(c))
					NEXT();
				TOKEN(TOK_INT);
			}

			is_double = 0;
			while ((c = PEEK())) {
				if (c == '.' && !is_double) {
					is_double = 1;
				} else if (c == '.' && is_double) {
					ERROR("Unexpected '.'");
					return 1;
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
				len += 1;
				if (isalnum(c) || c == '_');
				else break;
				(void) NEXT();
			}

			/* Check if it was a keyword */
			for (i = 0; i < LENGTH(KEYWORDS); i += 1) {
				if (len != KEYWORDS[i].len)
					continue;
				if (!strncmp(start_of_token,
							KEYWORDS[i].name,
							len))
					TOKEN(KEYWORDS[i].type);
			}
			TOKEN(TOK_IDENT);

#define READ_ESCAPE_SEQUENCE(C) ({ \
		if (c == '\\') { \
			if (strchr(VALID_ESCAPES, PEEK())) { \
				(void) NEXT(); \
			} else { \
				ERROR("Unrecognized escape sequence"); \
				return 1; \
			} \
		} \
	})

		case '\'':
			c = NEXT();
			READ_ESCAPE_SEQUENCE(c);
			if ((c = NEXT()) != '\'') {
				ERROR("Expected '\\'', got '%c'", c);
				return 1;
			}
			TOKEN(TOK_CHAR);

		case '"':
			while ((c = NEXT()) != '"') {
				READ_ESCAPE_SEQUENCE(c);
				if (c == '\n') {
					state->current_line += 1;
					state->current_column = 1;
				}
			}
			TOKEN(TOK_STRING);

		default:
			ERROR("Invalid token '%c'", c);
			return 1;
		}

#undef READ_ESCAPE_SEQUENCE
	}

	/* end of input */
	TOKEN(TOK_EOF);

#undef NEXT
#undef PEEK
#undef TOKEN
#undef OR2
#undef ERROR

end:
	dest->end = state->ptr - input;
	dest->start = start_of_token - input;
	return 0;
}

struct binding {
	struct binding *next;
	size_t name;
	size_t index;
	int is_upvalue;
};

struct scope {
	struct scope *parent;
	struct binding *locals,
		       *upvalues;
	size_t num_locals,
	       num_upvalues;
};

static struct scope scope_new()
{
	return (struct scope) {
		.parent = NULL,
		.locals = NULL,
		.upvalues = NULL,
		.num_locals = 0,
		.num_upvalues = 0,
	};
}

static void scope_free(struct scope s)
{
	struct binding *current, *tmp;

#define FREE_LIST(L) \
	do { \
		if (L) { \
			current = (L); \
			while (current) { \
				tmp = current; \
				current = current->next; \
				free(tmp); \
			} \
		} \
	} while (0)

	FREE_LIST(s.locals);
	FREE_LIST(s.upvalues);

#undef FREE_LIST
}

static size_t scope_add_binding(struct scope *s, size_t name, int upvalue)
{
	size_t id;
	struct binding *new_binding;

	new_binding = malloc(sizeof(struct binding));
	new_binding->name = name;
	new_binding->is_upvalue = upvalue;

	if (upvalue) {
		id = new_binding->index = s->num_upvalues++;
		new_binding->next = s->upvalues;
		s->upvalues = new_binding;
	} else {
		id = new_binding->index = s->num_locals++;
		new_binding->next = s->locals;
		s->locals = new_binding;
	}

	return id;
}

static struct binding *scope_get_binding(struct scope *s, size_t name)
{
	struct binding *binding;
#define SEARCH(L) \
	do { \
		if (!(L)) \
			break; \
		binding = (L); \
		while (binding) { \
			if (binding->name == name) \
				return binding; \
			binding = binding->next; \
		} \
	} while (0)

	SEARCH(s->locals);
	SEARCH(s->upvalues);

#undef SEARCH

	return NULL;
}

static int scope_has_binding(struct scope *s, size_t name)
{
	return scope_get_binding(s, name) != NULL;
}

static int scope_parent_has_binding(struct scope *s, size_t name)
{
	if (!s->parent)
		return 0;
	return scope_has_binding(s->parent, name)
		|| scope_parent_has_binding(s->parent, name);
}

static inline size_t tok_len(struct token *tok)
{
	return tok->end - tok->start;
}

struct pending_address {
	struct pending_address *next;
	size_t label, address;
};

struct bytecode {
	cb_instruction *code;
	size_t size, len;

	ssize_t *label_addresses;
	size_t label_addr_size, label_addr_len;

	struct pending_address *pending_addresses;
};

static struct bytecode *bytecode_new()
{
	struct bytecode *bc;

	bc = malloc(sizeof(struct bytecode));

	bc->code = NULL;
	bc->size = bc->len = 0;
	bc->label_addresses = NULL;
	bc->label_addr_size = bc->label_addr_len = 0;
	bc->pending_addresses = NULL;

	return bc;
}

inline cb_instruction cb_bytecode_get(struct bytecode *bc, size_t idx)
{
	return bc->code[idx];
}

inline size_t cb_bytecode_len(struct bytecode *bc)
{
	return bc->len;
}

inline void cb_bytecode_free(struct bytecode *bc)
{
	struct pending_address *current, *tmp;
	current = bc->pending_addresses;
	while (current) {
		tmp = current;
		current = current->next;
		free(tmp);
	}
	if (bc->code)
		free(bc->code);
	if (bc->label_addresses)
		free(bc->label_addresses);
	free(bc);
}

static size_t bytecode_label(struct bytecode *bc)
{
	if (bc->label_addr_size <= bc->label_addr_len) {
		bc->label_addr_size = bc->label_addr_size == 0
			? 4
			: bc->label_addr_size << 1;
		bc->label_addresses = realloc(bc->label_addresses,
				bc->label_addr_size * sizeof(size_t));
	}

	bc->label_addresses[bc->label_addr_len++] = -1;
	return bc->label_addr_len - 1;
}

static void bytecode_update_size_t(struct bytecode *bc, size_t idx,
		size_t value)
{
	size_t i;

	for (i = 0; i < sizeof(size_t) / sizeof(cb_instruction); i += 1) {
		bc->code[idx + i] = (value >> (i * 8 * sizeof(cb_instruction)))
			& (cb_instruction) -1;
	}
}

static void bytecode_mark_label(struct bytecode *bc, size_t label)
{
	struct pending_address *current, *tmp, **prev;

	assert(label < bc->label_addr_len);
	bc->label_addresses[label] = bc->len;

	prev = &bc->pending_addresses;
	current = bc->pending_addresses;
	while (current) {
		tmp = current;
		current = current->next;
		if (tmp->label == label) {
			bytecode_update_size_t(bc, tmp->address, bc->len);

			*prev = current;
			free(tmp);
		} else {
			prev = &tmp->next;
		}
	}
}

static void bytecode_push(struct bytecode *bc, cb_instruction byte)
{
	assert(bc != NULL);

	if (bc->size <= bc->len) {
		bc->size = bc->size == 0
			? INITIAL_BYTECODE_SIZE
			: bc->size << 2;
		bc->code = realloc(bc->code, bc->size * sizeof(cb_instruction));
	}

	bc->code[bc->len++] = byte;
}

static void bytecode_push_size_t(struct bytecode *bc, size_t value)
{
	int i;
	cb_instruction byte;

	for (i = 0; i < sizeof(size_t) / sizeof(cb_instruction); i += 1) {
		byte = value & (cb_instruction) -1;
		bytecode_push(bc, byte);
		value >>= 8 * sizeof(cb_instruction);
	}
}

static void bytecode_address_of(struct bytecode *bc, size_t label)
{
	size_t value;
	struct pending_address *addr;

	assert(label < bc->label_addr_len);

	if (bc->label_addresses[label] == -1) {
		value = 0;

		addr = malloc(sizeof(struct pending_address));
		addr->label = label;
		addr->address = bc->len;
		addr->next = bc->pending_addresses;
		bc->pending_addresses = addr;
	} else {
		value = bc->label_addresses[label];
	}

	bytecode_push_size_t(bc, value);
}

static int bytecode_finalize(struct bytecode *bc)
{
	if (bc->pending_addresses != NULL) {
		fprintf(stderr, "Missing label\n");
		return 1;
	}

	if (bc->label_addresses) {
		free(bc->label_addresses);
		bc->label_addresses = NULL;
	}
	bc->code = realloc(bc->code, bc->size * sizeof(cb_instruction));

	return 0;
}

struct function_state {
	size_t start_label;
	size_t end_label;
	size_t *free_variables;
	size_t free_var_len, free_var_size;
};

void fstate_add_freevar(struct function_state *fstate, size_t free_var)
{
	if (fstate->free_var_len >= fstate->free_var_size) {
		fstate->free_var_size = fstate->free_var_size == 0
			? 4
			: fstate->free_var_size << 1;
		fstate->free_variables = realloc(fstate->free_variables,
				fstate->free_var_size * sizeof(size_t));
	}
	fstate->free_variables[fstate->free_var_len++] = free_var;
}

struct loop_state {
	size_t continue_label;
	size_t break_label;
};

struct cstate {
	struct bytecode *bytecode;

	const char *input;
	const char *filename;

	int did_peek;
	struct token peeked;
	lex_state lex_state;

	cb_modspec *modspec;

	struct scope *scope;
	int is_global;

	struct function_state *function_state;
	struct loop_state *loop_state;

	cb_hashmap *imported;
};

static struct cstate cstate_default(int with_bytecode)
{
	return (struct cstate) {
		.bytecode = with_bytecode ? bytecode_new() : NULL,
		.input = NULL,
		.filename = "<script>",
		.did_peek = 0,
		.lex_state = lex_state_new("<script>"),
		.modspec = NULL,
		.is_global = 1,
		.scope = NULL,
		.function_state = NULL,
		.loop_state = NULL,
		.imported = cb_hashmap_new(),
	};
}

/* NOTE: does not free bytecode or input */
void cstate_free(struct cstate state)
{
	if (state.modspec)
		cb_modspec_free(state.modspec);
	cb_hashmap_free(state.imported);
}

static int resolve_binding(struct cstate *s, size_t name, struct binding *out)
{
	struct binding *binding;
	struct function_state *fstate;
	int i, found;

	if (!s->scope)
		return 0;
	binding = scope_get_binding(s->scope, name);
	if (binding) {
		*out = *binding;
		return 1;
	}
	if (scope_parent_has_binding(s->scope, name)) {
		fstate = s->function_state;
		assert(fstate != NULL);
		found = 0;
		for (i = 0; i < fstate->free_var_len; i += 1) {
			if (fstate->free_variables[i] == name) {
				found = 1;
				break;
			}
		}
		if (found) {
			out->is_upvalue = 1;
			out->name = name;
			out->index = i;
		} else {
			out->is_upvalue = 1;
			out->name = name;
			out->index = s->scope->num_upvalues
				+ s->function_state->free_var_len;
			fstate_add_freevar(fstate, name);
		}
		return 1;
	}

	return 0;
}

static inline const char *tok_start(struct cstate *state, struct token *tok)
{
	return state->input + tok->start;
}

static inline size_t intern_ident(struct cstate *state, struct token *tok)
{
	assert(tok->type == TOK_IDENT);
	return cb_agent_intern_string(tok_start(state, tok), tok_len(tok));
}

static struct token *peek(struct cstate *state, int *ok)
{
	if (state->did_peek) {
		*ok = 1;
		return &state->peeked;
	}
	state->did_peek = 1;
	*ok = !next_token(&state->lex_state, state->input, &state->peeked);
	return &state->peeked;
}

static struct token next(struct cstate *state, int *ok)
{
	struct token tok;
	if (state->did_peek) {
		*ok = 1;
		state->did_peek = 0;
		return state->peeked;
	} else {
		*ok = !next_token(&state->lex_state, state->input, &tok);
		return tok;
	}
}

/* These macros are expected to be used only within functions with a cstate*
 * variable named state */
#define PEEK() ({ \
		int _ok; \
		struct token *_tok; \
		_tok = peek(state, &_ok); \
		if (!_ok) \
			return 1; \
		_tok; \
	})
#define NEXT() ({ \
		int _ok; \
		struct token _tok = next(state, &_ok); \
		if (!_ok) \
			return 1; \
		_tok; \
	})
#define MATCH_P(TYPE) ({ \
		struct token *tok = PEEK(); \
		tok && tok->type != TOK_EOF && tok->type == (TYPE); \
	})
#define EXPECT(TYPE) ({ \
		struct token *_tok = PEEK(); \
		enum token_type typ = (TYPE); \
		if (!_tok || _tok->type != typ) { \
			enum token_type actual = _tok ? _tok->type \
							: TOK_EOF; \
			ERROR_AT_P(_tok, "Expected '%s', got '%s'", \
					token_type_name(typ), \
					token_type_name(actual)); \
			return 1; \
		} \
		NEXT(); \
	})
#define ERROR_AT_P(TOK, MSG, ...) ({ \
		if (TOK) { \
			ERROR_AT(*(TOK), MSG, ##__VA_ARGS__); \
		} else { \
			fprintf(stderr, "Error in %s at EOF: " MSG "\n", \
				state->filename, ##__VA_ARGS__); \
		} \
	})
#define ERROR_AT(TOK, MSG, ...) ({ \
		fprintf(stderr, "Error in %s at %zu:%zu: " MSG "\n", \
			state->filename, (TOK).line, (TOK).column, \
			##__VA_ARGS__); \
	})
#define APPEND(B) (bytecode_push(state->bytecode, (B)))
#define APPEND_SIZE_T(S) (bytecode_push_size_t(state->bytecode, (S)))
#define LABEL() (bytecode_label(state->bytecode))
#define MARK(L) (bytecode_mark_label(state->bytecode, (L)))
#define ADDR_OF(L) (bytecode_address_of(state->bytecode, (L)))
#define UPDATE(IDX, VAL) (bytecode_update_size_t(state->bytecode, (IDX), \
			(VAL)))

/* Propagate errors */
#define X(V) ({ if (V) return 1; })

static int compile_statement(struct cstate *);
static int compile_let_statement(struct cstate *, size_t *name_out, int leave);
static int compile_function_statement(struct cstate *, size_t *name_out,
		int leave);
static int compile_if_statement(struct cstate *);
static int compile_for_statement(struct cstate *);
static int compile_while_statement(struct cstate *);
static int compile_break_statement(struct cstate *);
static int compile_continue_statement(struct cstate *);
static int compile_return_statement(struct cstate *);
static int compile_export_statement(struct cstate *);
static int compile_import_statement(struct cstate *);
static int compile_struct_statement(struct cstate *, size_t *name_out,
		int leave);
static int compile_expression_statement(struct cstate *);

static int compile_expression(struct cstate *);

static int compile(struct cstate *state, size_t name_id, int final)
{
	struct token *tok;

	state->modspec = cb_modspec_new(name_id);

	APPEND(OP_INIT_MODULE);
	APPEND_SIZE_T(cb_modspec_id(state->modspec));

	while ((tok = PEEK()) && tok->type != TOK_EOF)
		X(compile_statement(state));

	EXPECT(TOK_EOF);

	assert(state->modspec && "Missing modspec while compiling");
	APPEND(OP_END_MODULE);
	cb_agent_add_modspec(state->modspec);
	state->modspec = NULL;

	if (final) {
		APPEND(OP_HALT);
		bytecode_finalize(state->bytecode);
#ifdef DEBUG_VM
		cb_agent_set_finished_compiling();
#endif
	}

	return 0;
}

static int compile_statement(struct cstate *state)
{
	switch (PEEK()->type) {
	case TOK_LET:
		X(compile_let_statement(state, NULL, 0));
		break;
	case TOK_FUNCTION:
		X(compile_function_statement(state, NULL, 0));
		break;
	case TOK_IF:
		X(compile_if_statement(state));
		break;
	case TOK_FOR:
		X(compile_for_statement(state));
		break;
	case TOK_WHILE:
		X(compile_while_statement(state));
		break;
	case TOK_BREAK:
		X(compile_break_statement(state));
		break;
	case TOK_CONTINUE:
		X(compile_continue_statement(state));
		break;
	case TOK_RETURN:
		X(compile_return_statement(state));
		break;
	case TOK_EXPORT:
		X(compile_export_statement(state));
		break;
	case TOK_IMPORT:
		X(compile_import_statement(state));
		break;
	case TOK_STRUCT:
		X(compile_struct_statement(state, NULL, 0));
		break;
	default:
		X(compile_expression_statement(state));
		break;
	}

	return 0;
}

static int compile_let_statement(struct cstate *state, size_t *name_out,
		int leave)
{
	struct token name;
	size_t name_id, binding_id;

	EXPECT(TOK_LET);
	name = EXPECT(TOK_IDENT);
	name_id = intern_ident(state, &name);
	if (name_out)
		*name_out = name_id;
	if (MATCH_P(TOK_EQUAL)) {
		EXPECT(TOK_EQUAL);
		X(compile_expression(state));
	} else {
		APPEND(OP_CONST_NULL);
	}
	EXPECT(TOK_SEMICOLON);

	if (state->is_global) {
		APPEND(OP_DECLARE_GLOBAL);
		APPEND_SIZE_T(name_id);
		APPEND(OP_STORE_GLOBAL);
		APPEND_SIZE_T(name_id);
		if (!leave)
			APPEND(OP_POP);
	} else {
		assert(state->scope != NULL);
		binding_id = scope_add_binding(state->scope, name_id, 0);
		APPEND(OP_STORE_LOCAL);
		APPEND_SIZE_T(binding_id);
		APPEND(OP_POP);
	}

	return 0;
}

static int compile_function(struct cstate *state, size_t *name_out)
{
	int first_param, old_is_global;
	struct token name, param;
	size_t name_id, binding_id;
	size_t num_params, local_count_pos;
	struct scope inner_scope, *old_scope;
	struct function_state fstate, *old_fstate;
	size_t free_var;
	struct binding *binding;
	int i, j;
	size_t start_label, end_label;

	EXPECT(TOK_FUNCTION);
	if (MATCH_P(TOK_IDENT)) {
		name = EXPECT(TOK_IDENT);
		name_id = intern_ident(state, &name);
		if (name_out)
			*name_out = name_id;
	} else {
		name_id = (size_t) -1;
	}

	inner_scope = scope_new();
	inner_scope.parent = state->scope;

	num_params = 0;
	first_param = 1;

	EXPECT(TOK_LEFT_PAREN);
	while (!MATCH_P(TOK_RIGHT_PAREN)) {
		if (first_param) {
			first_param = 0;
		} else {
			EXPECT(TOK_COMMA);
			/* support trailing comma */
			if (MATCH_P(TOK_RIGHT_PAREN))
				break;
		}
		num_params += 1;
		param = EXPECT(TOK_IDENT);
		scope_add_binding(&inner_scope, intern_ident(state, &param), 0);
	}
	EXPECT(TOK_RIGHT_PAREN);
	EXPECT(TOK_LEFT_BRACE);

	start_label = LABEL();
	end_label = LABEL();
	APPEND(OP_NEW_FUNCTION);
	APPEND_SIZE_T(name_id);
	APPEND_SIZE_T(num_params);
	ADDR_OF(start_label);

	if (state->is_global && name_id != -1) {
		APPEND(OP_DECLARE_GLOBAL);
		APPEND_SIZE_T(name_id);
		APPEND(OP_STORE_GLOBAL);
		APPEND_SIZE_T(name_id);
	} else if (name_id != -1) {
		assert(state->scope != NULL);
		binding_id = scope_add_binding(state->scope, name_id,
				0);
		APPEND(OP_STORE_LOCAL);
		APPEND_SIZE_T(binding_id);
	}

	APPEND(OP_JUMP);
	ADDR_OF(end_label);

	old_is_global = state->is_global;
	state->is_global = 0;
	old_scope = state->scope;
	state->scope = &inner_scope;

	MARK(start_label);
	APPEND(OP_ALLOCATE_LOCALS);
	local_count_pos = cb_bytecode_len(state->bytecode);
	APPEND_SIZE_T(0);

	fstate = (struct function_state) {
		.start_label = start_label,
		.end_label = end_label,
		.free_variables = NULL,
		.free_var_len = 0,
		.free_var_size = 0,
	};
	old_fstate = state->function_state;
	state->function_state = &fstate;

	while (!MATCH_P(TOK_RIGHT_BRACE))
		X(compile_statement(state));

	/* potentially an extra return, but better safe than sorry */
	APPEND(OP_CONST_NULL);
	APPEND(OP_RETURN);

	MARK(end_label);

	/* Make room on the stack for the local variables. Arguments will
	 * already be on the stack, so we don't need to allocate more room for
	 * them. */
	UPDATE(local_count_pos, inner_scope.num_locals - num_params);

	EXPECT(TOK_RIGHT_BRACE);

	if (fstate.free_var_len > 0)
		assert(old_scope != NULL);
	j = 0;
	for (i = 0; i < fstate.free_var_len; i += 1) {
		free_var = fstate.free_variables[i];
		binding = scope_get_binding(old_scope, free_var);
		if (binding != NULL) {
			if (binding->is_upvalue)
				APPEND(OP_BIND_UPVALUE);
			else
				APPEND(OP_BIND_LOCAL);
			APPEND_SIZE_T(binding->index);
		} else {
			if (scope_parent_has_binding(old_scope, free_var)) {
				assert(old_fstate != NULL);
				fstate_add_freevar(old_fstate, free_var);
				APPEND(OP_BIND_UPVALUE);
				APPEND_SIZE_T(old_scope->num_upvalues + j++);
			}
		}
	}

	free(fstate.free_variables);
	scope_free(inner_scope);
	state->scope = old_scope;
	state->is_global = old_is_global;
	state->function_state = old_fstate;

	return 0;
}

static int compile_function_statement(struct cstate *state, size_t *name_out,
		int leave)
{
	size_t name;

	X(compile_function(state, &name));
	if (name_out)
		*name_out = name;
	if (!leave || !state->is_global)
		APPEND(OP_POP);

	return 0;
}

static int compile_if_statement(struct cstate *state)
{
	size_t else_label, end_label;

	EXPECT(TOK_IF);
	EXPECT(TOK_LEFT_PAREN);

	else_label = LABEL();
	end_label = LABEL();

	/* predicate */
	X(compile_expression(state));

	APPEND(OP_JUMP_IF_FALSE);
	ADDR_OF(else_label);

	EXPECT(TOK_RIGHT_PAREN);
	EXPECT(TOK_LEFT_BRACE);

	while (!MATCH_P(TOK_RIGHT_BRACE))
		X(compile_statement(state));

	EXPECT(TOK_RIGHT_BRACE);

	APPEND(OP_JUMP);
	ADDR_OF(end_label);
	MARK(else_label);

	if (MATCH_P(TOK_ELSE)) {
		EXPECT(TOK_ELSE);

		/* allow "else if" vs "else { if ... }" */
		if (MATCH_P(TOK_IF)) {
			X(compile_if_statement(state));
		} else {
			EXPECT(TOK_LEFT_BRACE);
			while (!MATCH_P(TOK_RIGHT_BRACE))
				X(compile_statement(state));
			EXPECT(TOK_RIGHT_BRACE);
		}
	}

	MARK(end_label);

	return 0;
}

static int compile_for_statement(struct cstate *state)
{
	/* need more labels and jumps than rust-bytecode-vm since we do this in
	 * one pass while reading the input (can't put the increment at the end
	 * of the body) */
	size_t start_label, end_label, increment_label, body_label;
	struct loop_state lstate, *old_lstate;

	start_label = LABEL();
	end_label = LABEL();
	increment_label = LABEL();
	body_label = LABEL();

	lstate.continue_label = increment_label;
	lstate.break_label = end_label;

	EXPECT(TOK_FOR);
	EXPECT(TOK_LEFT_PAREN);
	
	/* if there is an initializer */
	if (!MATCH_P(TOK_SEMICOLON)) {
		X(compile_statement(state));
	} else {
		/* a semicolon will be matched by compile_statement, so we only
		 * do this if there isn't an initializer */
		EXPECT(TOK_SEMICOLON);
	}

	MARK(start_label);

	/* if there is a predicate */
	if (!MATCH_P(TOK_SEMICOLON)) {
		X(compile_expression(state));
		APPEND(OP_JUMP_IF_FALSE);
		ADDR_OF(end_label);
	}
	EXPECT(TOK_SEMICOLON);

	APPEND(OP_JUMP);
	ADDR_OF(body_label);

	MARK(increment_label);

	/* if there is an increment */
	if (!MATCH_P(TOK_RIGHT_PAREN)) {
		X(compile_expression(state));
		APPEND(OP_POP);
	}
	EXPECT(TOK_RIGHT_PAREN);
	EXPECT(TOK_LEFT_BRACE);

	APPEND(OP_JUMP);
	ADDR_OF(start_label);
	MARK(body_label);

	old_lstate = state->loop_state;
	state->loop_state = &lstate;

	while (!MATCH_P(TOK_RIGHT_BRACE))
		X(compile_statement(state));

	EXPECT(TOK_RIGHT_BRACE);
	APPEND(OP_JUMP);
	ADDR_OF(increment_label);
	MARK(end_label);

	state->loop_state = old_lstate;

	return 0;
}

static int compile_while_statement(struct cstate *state)
{
	size_t start_label, end_label;
	struct loop_state lstate, *old_lstate;

	start_label = LABEL();
	end_label = LABEL();

	lstate.break_label = end_label;
	lstate.continue_label = start_label;

	EXPECT(TOK_WHILE);
	EXPECT(TOK_LEFT_PAREN);

	MARK(start_label);
	X(compile_expression(state));
	APPEND(OP_JUMP_IF_FALSE);
	ADDR_OF(end_label);

	old_lstate = state->loop_state;
	state->loop_state = &lstate;

	EXPECT(TOK_RIGHT_PAREN);
	EXPECT(TOK_LEFT_BRACE);
	while (!MATCH_P(TOK_RIGHT_BRACE))
		X(compile_statement(state));
	EXPECT(TOK_RIGHT_BRACE);

	APPEND(OP_JUMP);
	ADDR_OF(start_label);
	MARK(end_label);

	state->loop_state = old_lstate;

	return 0;
}

static int compile_break_statement(struct cstate *state)
{
	struct token tok;

	tok = EXPECT(TOK_BREAK);
	if (!state->loop_state) {
		ERROR_AT(tok, "Break outside of loop");
		return 1;
	}
	EXPECT(TOK_SEMICOLON);

	APPEND(OP_JUMP);
	ADDR_OF(state->loop_state->break_label);

	return 0;
}

static int compile_continue_statement(struct cstate *state)
{
	struct token tok;

	tok = EXPECT(TOK_CONTINUE);
	if (!state->loop_state) {
		ERROR_AT(tok, "Continue outside of loop");
		return 1;
	}
	EXPECT(TOK_SEMICOLON);

	APPEND(OP_JUMP);
	ADDR_OF(state->loop_state->continue_label);

	return 0;
}

static int compile_return_statement(struct cstate *state)
{
	struct token tok;

	tok = EXPECT(TOK_RETURN);
	if (!state->function_state) {
		ERROR_AT(tok, "Return outside of function");
		return 1;
	}

	if (!MATCH_P(TOK_SEMICOLON))
		X(compile_expression(state));
	else
		APPEND(OP_CONST_NULL);
	EXPECT(TOK_SEMICOLON);

	APPEND(OP_RETURN);

	return 0;
}

static int compile_export_statement(struct cstate *state)
{
	struct token tok;
	size_t name;

	tok = EXPECT(TOK_EXPORT);
	assert(state->modspec && "modspec not present while compiling");

	if (!state->is_global) {
		ERROR_AT(tok, "Can only export from global scope");
		return 1;
	}

	if (MATCH_P(TOK_LET)) {
		X(compile_let_statement(state, &name, 1));
	} else if (MATCH_P(TOK_FUNCTION)) {
		X(compile_function_statement(state, &name, 1));
	} else if (MATCH_P(TOK_STRUCT)) {
		X(compile_struct_statement(state, &name, 1));
	} else {
		ERROR_AT_P(PEEK(), "Can only export function, let, and struct declarations");
		return 1;
	}

	APPEND(OP_EXPORT);
	APPEND_SIZE_T(cb_modspec_add_export(state->modspec, name));

	return 0;
}

static int compile_file(struct cstate *state, size_t name, const char *path,
		FILE *f, int final);

static int compile_import_statement(struct cstate *state)
{
	struct token tok, ident;
	char *filename, *modpath;
	cb_str modsrc;
	size_t modname;
	const struct cb_builtin_module_spec *builtin;
	FILE *f;

	tok = EXPECT(TOK_IMPORT);
	if (!state->is_global) {
		ERROR_AT(tok, "Import must be at top level");
		return 1;
	}

	ident = EXPECT(TOK_IDENT);
	EXPECT(TOK_SEMICOLON);

	modname = intern_ident(state, &ident);
	modsrc = cb_agent_get_string(modname);
	builtin = NULL;

	if (cb_hashmap_get(state->imported, modname))
		return 0;

	/* If the import name is one of the names is one of the builtin
	 * modules, just add that builtin module to state->imported */
	for (size_t i = 0; i < cb_builtin_module_count; i += 1) {
		size_t builtin_name_len = strlen(cb_builtin_modules[i].name);
		if (!cb_str_eq_cstr(modsrc, cb_builtin_modules[i].name,
				builtin_name_len)) {
			builtin = &cb_builtin_modules[i];
			break;
		}
	}

	if (!builtin) {
		/* FIXME: this sucks */
		if (strlen(state->filename) == sizeof("<script>") - 1
				&& !strncmp("<script>", state->filename,
					sizeof("<script>") - 1)) {
			filename = malloc(1);
			*filename = 0;
		} else {
			char *real_path = realpath(state->filename, NULL);
			const char *dir_name = dirname(real_path);
			size_t len = strlen(dir_name);
			filename = malloc(len + 1);
			filename[len] = 0;
			memcpy(filename, dir_name, strlen(dir_name));
			free(real_path);
		}

		APPEND(OP_EXIT_MODULE);
		f = cb_agent_resolve_import(modsrc, filename, &modpath);
		free(filename);
		if (!f)
			goto error;
		filename = strndup(cb_strptr(modsrc), cb_strlen(modsrc));
		X(compile_file(state, modname, filename, f, 0));
		free(filename);
		assert(state->modspec && "Missing modspec while compiling");
		APPEND(OP_ENTER_MODULE);
		APPEND_SIZE_T(cb_modspec_id(state->modspec));
	}

	cb_hashmap_set(state->imported, modname,
			(struct cb_value) { .type = CB_VALUE_NULL });

	return 0;

error:
	return 1;
}

static int compile_struct_decl(struct cstate *state, size_t *name_out)
{
	struct token name, field;
	struct cb_struct_spec spec;
	size_t name_id, num_fields, spec_id, binding_id;
	int first_field;

	EXPECT(TOK_STRUCT);
	if (MATCH_P(TOK_IDENT)) {
		name = EXPECT(TOK_IDENT);
		name_id = intern_ident(state, &name);
		if (name_out)
			*name_out = name_id;
	} else {
		name_id = (size_t) -1;
	}

	num_fields = 0;
	first_field = 1;
	cb_struct_spec_init(&spec, name_id);

	EXPECT(TOK_LEFT_BRACE);
	while (!MATCH_P(TOK_RIGHT_BRACE)) {
		if (first_field) {
			first_field = 0;
		} else {
			EXPECT(TOK_COMMA);
			/* support trailing comma */
			if (MATCH_P(TOK_RIGHT_BRACE))
				break;
		}
		num_fields += 1;
		field = EXPECT(TOK_IDENT);
		cb_struct_spec_add_field(&spec, intern_ident(state, &field));
	}
	EXPECT(TOK_RIGHT_BRACE);

	spec_id = cb_agent_add_struct_spec(spec);
	APPEND(OP_NEW_STRUCT_SPEC);
	APPEND_SIZE_T(spec_id);

	if (state->is_global && name_id != -1) {
		APPEND(OP_DECLARE_GLOBAL);
		APPEND_SIZE_T(name_id);
		APPEND(OP_STORE_GLOBAL);
		APPEND_SIZE_T(name_id);
	} else if (name_id != -1) {
		assert(state->scope != NULL);
		binding_id = scope_add_binding(state->scope, name_id, 0);
		APPEND(OP_STORE_LOCAL);
		APPEND_SIZE_T(binding_id);
	}

	return 0;
}

static int compile_struct_statement(struct cstate *state, size_t *name_out,
		int leave)
{
	size_t name_id;

	X(compile_struct_decl(state, &name_id));

	if (name_out)
		*name_out = name_id;
	if (!leave || !state->is_global)
		APPEND(OP_POP);

	return 0;
}

static int compile_expression_statement(struct cstate *state)
{
	X(compile_expression(state));
	EXPECT(TOK_SEMICOLON);
	APPEND(OP_POP);

	return 0;
}

static int lbp(enum token_type op)
{
	switch (op) {
	case TOK_IDENT:
	case TOK_INT:
	case TOK_DOUBLE:
	case TOK_STRING:
	case TOK_CHAR:
	case TOK_NULL:
	case TOK_TRUE:
	case TOK_FALSE:
	case TOK_SEMICOLON:
	case TOK_RIGHT_PAREN:
	case TOK_RIGHT_BRACKET:
	case TOK_COMMA:
		return 0;

	case TOK_EQUAL: return 1;
	case TOK_PIPE_PIPE: return 2;
	case TOK_AND_AND: return 3;
	case TOK_PIPE: return 4;
	case TOK_CARET: return 5;
	case TOK_AND: return 6;

	case TOK_BANG_EQUAL:
	case TOK_EQUAL_EQUAL:
		return 7;

	case TOK_LESS_THAN:
	case TOK_LESS_THAN_EQUAL:
	case TOK_GREATER_THAN:
	case TOK_GREATER_THAN_EQUAL:
		return 8;

	/* TODO: Shift operators
	case TOK_LESS_LESS:
	case TOK_GREATER_GREATER:
		return 9; */

	case TOK_PLUS:
	case TOK_MINUS:
		return 10;

	case TOK_STAR: return 11;
	case TOK_SLASH: return 12;
	case TOK_PERCENT: return 13;
	case TOK_STAR_STAR: return 14;

	case TOK_LEFT_PAREN:
	case TOK_LEFT_BRACKET:
	case TOK_COLON:
	case TOK_LEFT_BRACE:
		return 16;

	case TOK_DOT:
		return 17;

	default:
		return -1;
	}
}

static int rbp(enum token_type op)
{
	switch (op) {
	case TOK_LEFT_PAREN:
	case TOK_LEFT_BRACKET:
		return 0;

	case TOK_BANG:
	case TOK_MINUS:
	case TOK_TILDE:
		return 11;

	default:
		return -1;
	}
}

static int compile_int_expression(struct cstate *state)
{
	ssize_t num;
	size_t len;
	char c;
	int scanned;
	char *buf;
	struct token tok;

	tok = NEXT();
	assert(tok.type == TOK_INT);

	/* FIXME: find way to parse int from non-null-terminated string
	 * This allocation should not be necessary */
	len = tok_len(&tok);
	buf = malloc(len + 1);
	memcpy(buf, tok_start(state, &tok), len);
	buf[len] = 0;

	if (len > 1 && buf[1] == 'x')
		scanned = sscanf(buf, "0x%zx%c", &num, &c);
	else
		scanned = sscanf(buf, "%zd%c", &num, &c);
	free(buf);
	if (scanned == 1) {
		APPEND(OP_CONST_INT);
		APPEND_SIZE_T((size_t) num);
	} else {
		ERROR_AT(tok, "Invalid integer literal");
		return 1;
	}

	return 0;
}

static int compile_identifier_expression(struct cstate *state)
{
	struct token tok, export;
	struct binding binding;
	size_t name;
	int ok;
	cb_modspec *module;

	tok = EXPECT(TOK_IDENT);
	name = intern_ident(state, &tok);

	if (MATCH_P(TOK_DOT)) {
		NEXT();
		export = EXPECT(TOK_IDENT);
		APPEND(OP_LOAD_FROM_MODULE);
		module = cb_agent_get_modspec_by_name(name);
		if (!module) {
			ERROR_AT(tok, "No such module %s\n",
					cb_strptr(cb_agent_get_string(
							intern_ident(state,
								&tok))));
			return 1;
		} else if (!cb_hashmap_get(state->imported, name)) {
			ERROR_AT(tok, "Missing import for module %s\n",
					cb_strptr(cb_agent_get_string(
							intern_ident(state,
								&tok))));
			return 1;
		}
		APPEND_SIZE_T(cb_modspec_id(module));
		APPEND_SIZE_T(cb_modspec_get_export_id(module,
					intern_ident(state, &export), &ok));
		if (!ok) {
			ERROR_AT(export, "Module %s has no export %s",
					cb_strptr(cb_agent_get_string(
							cb_modspec_name(
								module))),
					cb_strptr(cb_agent_get_string(
							intern_ident(state,
								&export))));
			return 1;
		}
		return 0;
	}

	if (MATCH_P(TOK_EQUAL)) {
		NEXT();
		X(compile_expression(state));
		if (!resolve_binding(state, name, &binding)) {
			APPEND(OP_STORE_GLOBAL);
			APPEND_SIZE_T(name);
			return 0;
		}
		if (binding.is_upvalue) {
			assert(state->function_state != NULL);
			APPEND(OP_STORE_UPVALUE);
		} else {
			APPEND(OP_STORE_LOCAL);
		}
		APPEND_SIZE_T(binding.index);
		return 0;
	}

	if (resolve_binding(state, name, &binding)) {
		if (binding.is_upvalue) {
			assert(state->function_state != NULL);
			APPEND(OP_LOAD_UPVALUE);
		} else {
			APPEND(OP_LOAD_LOCAL);
		}
		APPEND_SIZE_T(binding.index);
	} else {
		APPEND(OP_LOAD_GLOBAL);
		APPEND_SIZE_T(name);
	}

	return 0;
}

static int compile_double_expression(struct cstate *state)
{
	struct token tok;
	double doub;
	char *buf;
	size_t len;

	tok = EXPECT(TOK_DOUBLE);

	/* FIXME: find way to parse double from non-null-terminated string
	 * This allocation should not be necessary */
	len = tok_len(&tok);
	buf = malloc(len + 1);
	memcpy(buf, tok_start(state, &tok), len);
	buf[len] = 0;

	doub = strtod(buf, NULL);
	free(buf);

	APPEND(OP_CONST_DOUBLE);
	APPEND_SIZE_T(*(size_t *) &doub); /* 😨 */

	return 0;
}

#define TRANSLATE_ESCAPE(C) ({ \
		char _c = (C); \
		switch (_c) { \
		case 'n': \
			_c = '\n'; \
			break; \
		case 'r': \
			_c = '\r'; \
			break; \
		case 't': \
			_c = '\t'; \
			break; \
		case '"': \
			_c = '"'; \
			break; \
		case '\'': \
			_c = '\''; \
			break; \
		} \
		_c; \
	})

static int compile_string_expression(struct cstate *state)
{
	struct token tok;
	size_t id;
	char *str, *ptr, c;

	tok = EXPECT(TOK_STRING);
	id = cb_agent_intern_string(tok_start(state, &tok) + 1,
			tok_len(&tok) - 2);

	ptr = str = cb_agent_get_string(id).chars;
	while ((c = *str++)) {
		if (c != '\\') {
			*ptr++ = c;
			continue;
		}
		*ptr++ = TRANSLATE_ESCAPE(*str++);
	}
	*ptr = 0;

	APPEND(OP_CONST_STRING);
	APPEND_SIZE_T(id);

	return 0;
}

static int compile_char_expression(struct cstate *state)
{
	struct token tok;
	size_t c;

	tok = EXPECT(TOK_CHAR);
	/* FIXME: support unicode */
	c = tok_start(state, &tok)[1];
	if (c == '\\')
		c = TRANSLATE_ESCAPE(tok_start(state, &tok)[2]);

	APPEND(OP_CONST_CHAR);
	APPEND_SIZE_T(c);

	return 0;
}

static int compile_parenthesized_expression(struct cstate *state)
{
	EXPECT(TOK_LEFT_PAREN);
	X(compile_expression(state));
	EXPECT(TOK_RIGHT_PAREN);

	return 0;
}

static int compile_array_expression(struct cstate *state)
{
	size_t num_elements;
	int first_elem;

	num_elements = 0;
	first_elem = 1;

	EXPECT(TOK_LEFT_BRACKET);
	while (!MATCH_P(TOK_RIGHT_BRACKET)) {
		if (first_elem) {
			first_elem = 0;
		} else {
			EXPECT(TOK_COMMA);
			if (MATCH_P(TOK_RIGHT_BRACKET))
				break;
		}
		num_elements += 1;
		X(compile_expression(state));
	}
	EXPECT(TOK_RIGHT_BRACKET);

	APPEND(OP_NEW_ARRAY_WITH_VALUES);
	APPEND_SIZE_T(num_elements);

	return 0;
}

static int compile_expression_inner(struct cstate *, int rbp);

static int compile_unary_expression(struct cstate *state)
{
	struct token tok;

	tok = NEXT();
	X(compile_expression_inner(state, rbp(tok.type)));

	switch (tok.type) {
	case TOK_MINUS:
		APPEND(OP_NEG);
		break;
	case TOK_BANG:
		APPEND(OP_NOT);
		break;
	case TOK_TILDE:
		APPEND(OP_BITWISE_NOT);
		break;
	default:
		ERROR_AT(tok, "Invalid unary operator");
		return 1;
	}

	return 0;
}

static int nud(struct cstate *state)
{
	switch (PEEK()->type) {
	case TOK_IDENT:
		X(compile_identifier_expression(state));
		break;
	case TOK_INT:
		X(compile_int_expression(state));
		break;
	case TOK_DOUBLE:
		X(compile_double_expression(state));
		break;
	case TOK_STRING:
		X(compile_string_expression(state));
		break;
	case TOK_CHAR:
		X(compile_char_expression(state));
		break;
	case TOK_NULL:
		EXPECT(TOK_NULL);
		APPEND(OP_CONST_NULL);
		break;
	case TOK_TRUE:
		EXPECT(TOK_TRUE);
		APPEND(OP_CONST_TRUE);
		break;
	case TOK_FALSE:
		EXPECT(TOK_FALSE);
		APPEND(OP_CONST_FALSE);
		break;
	case TOK_LEFT_PAREN:
		X(compile_parenthesized_expression(state));
		break;
	case TOK_LEFT_BRACKET:
		X(compile_array_expression(state));
		break;
	case TOK_MINUS:
	case TOK_BANG:
	case TOK_TILDE:
		X(compile_unary_expression(state));
		break;
	case TOK_FUNCTION:
		X(compile_function(state, NULL));
		break;
	case TOK_STRUCT:
		X(compile_struct_decl(state, NULL));
		break;

	default:
		ERROR_AT_P(PEEK(), "Expected expression");
		return 1;
	}

	return 0;
}

static int compile_left_assoc_binary(struct cstate *state)
{
	struct token tok;

	tok = NEXT();
	X(compile_expression_inner(state, lbp(tok.type)));

	switch (tok.type) {
	case TOK_PLUS:
		APPEND(OP_ADD);
		break;
	case TOK_MINUS:
		APPEND(OP_SUB);
		break;
	case TOK_STAR:
		APPEND(OP_MUL);
		break;
	case TOK_SLASH:
		APPEND(OP_DIV);
		break;
	case TOK_PERCENT:
		APPEND(OP_MOD);
		break;
	case TOK_LESS_THAN:
		APPEND(OP_LESS_THAN);
		break;
	case TOK_LESS_THAN_EQUAL:
		APPEND(OP_LESS_THAN_EQUAL);
		break;
	case TOK_GREATER_THAN:
		APPEND(OP_GREATER_THAN);
		break;
	case TOK_GREATER_THAN_EQUAL:
		APPEND(OP_GREATER_THAN_EQUAL);
		break;
	case TOK_EQUAL_EQUAL:
		APPEND(OP_EQUAL);
		break;
	case TOK_BANG_EQUAL:
		APPEND(OP_NOT_EQUAL);
		break;
	case TOK_PIPE:
		APPEND(OP_BITWISE_OR);
		break;
	case TOK_CARET:
		APPEND(OP_BITWISE_XOR);
		break;
	case TOK_AND:
		APPEND(OP_BITWISE_AND);
		break;

	default:
		ERROR_AT(tok, "Unexpected token");
		return 1;
	}

	return 0;
}

static int compile_right_assoc_binary(struct cstate *state)
{
	struct token tok;

	tok = NEXT();
	X(compile_expression_inner(state, lbp(tok.type) - 1));

	if (tok.type == TOK_STAR_STAR)
		APPEND(OP_EXP);

	return 0;
}

static int compile_call_expression(struct cstate *state)
{
	size_t num_args;
	int first_arg;

	EXPECT(TOK_LEFT_PAREN);
	num_args = 0;

	first_arg = 1;
	while (!MATCH_P(TOK_RIGHT_PAREN)) {
		if (first_arg) {
			first_arg = 0;
		} else {
			EXPECT(TOK_COMMA);
			if (MATCH_P(TOK_RIGHT_PAREN))
				break;
		}

		num_args += 1;
		X(compile_expression(state));
	}
	EXPECT(TOK_RIGHT_PAREN);

	APPEND(OP_CALL);
	APPEND_SIZE_T(num_args);

	return 0;
}

static int compile_index_expression(struct cstate *state)
{
	EXPECT(TOK_LEFT_BRACKET);
	X(compile_expression(state));
	EXPECT(TOK_RIGHT_BRACKET);

	if (MATCH_P(TOK_EQUAL)) {
		EXPECT(TOK_EQUAL);
		X(compile_expression(state));
		APPEND(OP_ARRAY_SET);
	} else {
		APPEND(OP_ARRAY_GET);
	}

	return 0;
}

static int compile_struct_field_expression(struct cstate *state)
{
	struct token fname_tok;
	size_t name;

	EXPECT(TOK_COLON);
	fname_tok = EXPECT(TOK_IDENT);
	name = intern_ident(state, &fname_tok);

	if (MATCH_P(TOK_EQUAL)) {
		EXPECT(TOK_EQUAL);
		X(compile_expression(state));
		APPEND(OP_STORE_STRUCT);
	} else {
		APPEND(OP_LOAD_STRUCT);
	}

	APPEND_SIZE_T(name);
	return 0;
}

static int compile_struct_expression(struct cstate *state)
{
	int first_field;
	struct token name_tok;
	size_t name;

	EXPECT(TOK_LEFT_BRACE);
	APPEND(OP_NEW_STRUCT);

	first_field = 1;
	while (!MATCH_P(TOK_RIGHT_BRACE)) {
		if (first_field) {
			first_field = 0;
		} else {
			EXPECT(TOK_COMMA);
			if (MATCH_P(TOK_RIGHT_BRACE))
				break;
		}
		name_tok = EXPECT(TOK_IDENT);
		name = intern_ident(state, &name_tok);
		EXPECT(TOK_EQUAL);
		X(compile_expression(state));
		APPEND(OP_ADD_STRUCT_FIELD);
		APPEND_SIZE_T(name);
	}
	EXPECT(TOK_RIGHT_BRACE);

	return 0;
}

static int compile_short_circuit_binary(struct cstate *state)
{
	struct token tok;
	size_t end_label;

	tok = NEXT();
	end_label = LABEL();
	APPEND(OP_DUP);

	switch (tok.type) {
	case TOK_AND_AND:
		APPEND(OP_JUMP_IF_FALSE);
		break;
	case TOK_PIPE_PIPE:
		APPEND(OP_JUMP_IF_TRUE);
		break;
	default:
		abort(); /* unreachable */
	}

	ADDR_OF(end_label);
	APPEND(OP_POP);
	X(compile_expression_inner(state, lbp(tok.type)));
	MARK(end_label);

	return 0;
}

static int led(struct cstate *state)
{
	switch (PEEK()->type) {
	case TOK_PLUS:
	case TOK_MINUS:
	case TOK_STAR:
	case TOK_SLASH:
	case TOK_PERCENT:
	case TOK_LESS_THAN:
	case TOK_LESS_THAN_EQUAL:
	case TOK_GREATER_THAN:
	case TOK_GREATER_THAN_EQUAL:
	case TOK_EQUAL_EQUAL:
	case TOK_BANG_EQUAL:
	case TOK_PIPE:
	case TOK_CARET:
	case TOK_AND:
		X(compile_left_assoc_binary(state));
		break;

	case TOK_AND_AND:
	case TOK_PIPE_PIPE:
		X(compile_short_circuit_binary(state));
		break;

	case TOK_EQUAL:
	case TOK_STAR_STAR:
		X(compile_right_assoc_binary(state));
		break;

	case TOK_LEFT_PAREN:
		X(compile_call_expression(state));
		break;

	case TOK_LEFT_BRACKET:
		X(compile_index_expression(state));
		break;

	case TOK_COLON:
		X(compile_struct_field_expression(state));
		break;

	case TOK_LEFT_BRACE:
		X(compile_struct_expression(state));
		break;

	default:
		ERROR_AT_P(PEEK(), "Unexpected token");
		return 1;
	}

	return 0;
}

static int compile_expression_inner(struct cstate *state, int rbp)
{
	X(nud(state));

	while (PEEK() && PEEK()->type != TOK_EOF && rbp < lbp(PEEK()->type))
		X(led(state));

	return 0;
}

static int compile_expression(struct cstate *state)
{
	return compile_expression_inner(state, 0);
}

int cb_compile(const char *input, struct bytecode **bc_out)
{
	int result;
	struct cstate state = cstate_default(1);
	state.input = input;

	result = compile(&state, cb_agent_intern_string("<string>", 8), 1);

	cstate_free(state);
	*bc_out = state.bytecode;

	return result;
}

static int read_file(FILE *f, char **out)
{
	int result;
	size_t filesize;

	assert(out != NULL);
	result = 0;
	*out = NULL;

#define HANDLE_ERROR(P, ...) \
	do { \
		if (P) { \
			__VA_ARGS__; \
			result = 1; \
			goto error; \
		} \
	} while (0)

	/* FIXME: Return error code so caller can report */
	HANDLE_ERROR(fseek(f, 0, SEEK_END), perror("fseek"));
	filesize = ftell(f);
	HANDLE_ERROR(fseek(f, 0, SEEK_SET), perror("fseek"));

	*out = malloc(sizeof(char) * filesize + 1);
	fread(*out, sizeof(char), filesize, f);
	(*out)[filesize] = 0;

	goto epilogue;

#undef HANDLE_ERROR

error:
	if (*out)
		free(*out);
epilogue:
	if (f)
		fclose(f);
	return result;
}

static int compile_file(struct cstate *state, size_t name, const char *path,
		FILE *f, int final)
{
	int result;
	char *input;
	struct cstate new_state;

	assert(state != NULL);
	if (cb_agent_get_modspec_by_name(name))
		return 0;

	if (read_file(f, &input))
		return 1;

	new_state = cstate_default(0);
	new_state.bytecode = state->bytecode;
	new_state.input = input;
	new_state.lex_state = lex_state_new(path);
	new_state.filename = path;

	result = compile(&new_state, name, final);

	free(input);
	cstate_free(new_state);
	return result;
}

int cb_compile_file(const char *name, const char *path,
		struct bytecode **bc_out)
{
	int result;
	FILE *f;
	struct cstate state;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "File not found: '%s'\n", path);
		return 1;
	}

	state = cstate_default(1);
	result = compile_file(&state,
			cb_agent_intern_string(name, strlen(name)), path, f, 1);
	*bc_out = state.bytecode;

	cstate_free(state);

	return result;
}
