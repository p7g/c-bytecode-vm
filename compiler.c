/* Here lies the entire c-bytecode-vm compiler. This includes lexing, parsing,
 * and code generation. The latter two happen at the same time. */

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "compiler.h"
#include "module.h"
#include "opcode.h"

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
	X(TOK_IMPORT) \
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
	KW("module", TOK_MODULE),
	KW("export", TOK_EXPORT),
	KW("import", TOK_IMPORT),
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
	uint8_t *code;
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

inline uint8_t cb_bytecode_get(struct bytecode *bc, size_t idx)
{
	return bc->code[idx];
}

inline size_t cb_bytecode_len(struct bytecode *bc)
{
	return bc->len;
}

inline void cb_bytecode_free(struct bytecode *bc)
{
	assert(bc->label_addresses == NULL);
	assert(bc->pending_addresses == NULL);
	if (bc->code)
		free(bc->code);
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

	for (i = 0; i < sizeof(size_t); i += 1)
		bc->code[idx + i] = (value >> (i * 8)) & 0xFF;
}

static void bytecode_mark_label(struct bytecode *bc, size_t label)
{
	struct pending_address *current, *tmp, **prev;
	size_t i;

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

static void bytecode_push(struct bytecode *bc, uint8_t byte)
{
	assert(bc != NULL);

	if (bc->size <= bc->len) {
		bc->size = bc->size == 0
			? INITIAL_BYTECODE_SIZE
			: bc->size << 1;
		bc->code = realloc(bc->code, bc->size);
	}

	bc->code[bc->len++] = byte;
}

static void bytecode_push_size_t(struct bytecode *bc, size_t value)
{
	int i;
	uint8_t byte;

	for (i = 0; i < sizeof(size_t); i += 1) {
		byte = value & 0xFF;
		bytecode_push(bc, byte);
		value >>= 8;
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
	bc->code = realloc(bc->code, bc->size);

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
	size_t start_label;
	size_t end_label;
	size_t increment_label;
};

struct cstate {
	struct bytecode *bytecode;

	const char *input;
	const char *filename;

	int did_peek;
	struct token peeked;
	lex_state lex_state;

	int in_module;
	cb_module_spec modspec;

	struct scope *scope;
	int is_global;

	struct function_state *function_state;
};

static struct cstate cstate_default()
{
	return (struct cstate) {
		.bytecode = bytecode_new(),
		.input = NULL,
		.filename = "<script>",
		.did_peek = 0,
		.lex_state = lex_state_new("<script>"),
		.in_module = 0,
		.is_global = 1,
		.scope = NULL,
		.function_state = NULL,
	};
}

static int resolve_binding(struct cstate *s, size_t name, struct binding *out)
{
	struct binding *binding;
	struct function_state *fstate;
	int i, found;
	size_t idx;

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
		tok && tok->type == (TYPE); \
	})
#define EXPECT(TYPE) ({ \
		struct token *_tok = PEEK(); \
		enum token_type typ = (TYPE); \
		if (!_tok || _tok->type != typ) { \
			enum token_type actual = _tok ? _tok->type \
							: TOK_EOF; \
			ERROR_AT(_tok, "Expected '%s', got '%s'", \
					token_type_name(typ), \
					token_type_name(actual)); \
			return 1; \
		} \
		NEXT(); \
	})
#define ERROR_AT(TOK, MSG, ...) ({ \
		if (TOK) { \
			fprintf(stderr, "Error in %s at %zu:%zu: " MSG "\n", \
				state->filename, (TOK)->line, (TOK)->column, \
				##__VA_ARGS__); \
		} else { \
			fprintf(stderr, "Error in %s at EOF: " MSG "\n", \
				state->filename, ##__VA_ARGS__); \
		} \
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

static int compile_module_header(struct cstate *state)
{
	struct token name;

	if (!MATCH_P(TOK_MODULE))
		return 0;

	EXPECT(TOK_MODULE);
	name = EXPECT(TOK_IDENT);
	state->in_module = 1;
	state->modspec.name = cb_agent_intern_string(state->input + name.start,
			tok_len(&name));
	state->modspec.id = cb_agent_reserve_id();
	APPEND(OP_INIT_MODULE);
	APPEND_SIZE_T(state->modspec.id);

	return 0;
}

static int compile_statement(struct cstate *);
static int compile_let_statement(struct cstate *, int leave);
static int compile_function_statement(struct cstate *, int leave);
static int compile_if_statement(struct cstate *);
static int compile_for_statement(struct cstate *);
static int compile_while_statement(struct cstate *);
static int compile_break_statement(struct cstate *);
static int compile_continue_statement(struct cstate *);
static int compile_return_statement(struct cstate *);
static int compile_export_statement(struct cstate *);
static int compile_import_statement(struct cstate *);
static int compile_expression_statement(struct cstate *);

static int compile_expression(struct cstate *);

static int compile(struct cstate *state, int final)
{
	struct token *tok;

	/* optional module header */
	X(compile_module_header(state));

	while ((tok = PEEK()) && tok->type != TOK_EOF)
		X(compile_statement(state));

	EXPECT(TOK_EOF);

	if (state->in_module) {
		APPEND(OP_END_MODULE);
		state->in_module = 0;
	}

	if (final)
		bytecode_finalize(state->bytecode);

	return 0;
}

static int compile_statement(struct cstate *state)
{
	switch (PEEK()->type) {
	case TOK_LET:
		X(compile_let_statement(state, 0));
		break;
	case TOK_FUNCTION:
		X(compile_function_statement(state, 0));
		break;
	/*case TOK_IF:
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
	default:
		X(compile_expression_statement(state));
		break;
	*/}

	return 0;
}

static int compile_let_statement(struct cstate *state, int leave)
{
	struct token name;
	size_t name_id, binding_id;

	EXPECT(TOK_LET);
	name = EXPECT(TOK_IDENT);
	name_id = intern_ident(state, &name);
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
	}

	return 0;
}

static int compile_function(struct cstate *state, size_t *name_out)
{
	int first_param, old_is_global;
	struct token name, param;
	size_t name_id;
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
		*name_out = name_id = intern_ident(state, &name);
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
			if (scope_parent_has_binding(old_scope,
						binding->name)) {
				assert(old_fstate != NULL);
				fstate_add_freevar(old_fstate, free_var);
				APPEND(OP_BIND_UPVALUE);
				APPEND_SIZE_T(old_scope->num_upvalues + j++);
			}
		}
	}

	scope_free(inner_scope);
	state->scope = old_scope;
	state->is_global = old_is_global;
	state->function_state = old_fstate;

	return 0;
}

static int compile_function_statement(struct cstate *state, int leave)
{
	size_t name, binding_id;

	X(compile_function(state, &name));

	if (state->is_global) {
		APPEND(OP_DECLARE_GLOBAL);
		APPEND_SIZE_T(name);
		APPEND(OP_STORE_GLOBAL);
		APPEND_SIZE_T(name);
		if (!leave)
			APPEND(OP_POP);
	} else {
		assert(state->scope != NULL);
		binding_id = scope_add_binding(state->scope, name, 0);
		APPEND(OP_STORE_LOCAL);
		APPEND_SIZE_T(binding_id);
	}

	return 0;
}

static int compile_if_statement(struct cstate *);
static int compile_for_statement(struct cstate *);
static int compile_while_statement(struct cstate *);
static int compile_break_statement(struct cstate *);
static int compile_continue_statement(struct cstate *);
static int compile_return_statement(struct cstate *);
static int compile_export_statement(struct cstate *);
static int compile_import_statement(struct cstate *);
static int compile_expression_statement(struct cstate *);

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

	scanned = sscanf(buf, "%zd%c", &num, &c);
	free(buf);
	if (scanned == 1) {
		APPEND(OP_CONST_INT);
		APPEND_SIZE_T((size_t) num);
	} else {
		ERROR_AT(&tok, "Invalid integer literal");
		return 1;
	}

	return 0;
}

static int compile_identifier_expression(struct cstate *state)
{
	struct token tok;
	struct binding binding;
	size_t name;

	tok = NEXT();
	assert(tok.type == TOK_IDENT);
	name = intern_ident(state, &tok);

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

static int compile_expression(struct cstate *state)
{
	switch (PEEK()->type) {
	case TOK_INT:
		X(compile_int_expression(state));
		break;

	case TOK_IDENT:
		X(compile_identifier_expression(state));
		break;

	default:
		ERROR_AT(PEEK(), "Expected expression");
		return 1;
	}

	return 0;
}

int cb_compile(const char *input, struct bytecode **bc_out)
{
	int result;
	struct cstate state = cstate_default();
	state.input = input;

	result = compile(&state, 1);

	/* FIXME: clean up */
	*bc_out = state.bytecode;

	return result;
}

static int read_file(const char *name, char **out)
{
	int result;
	FILE *f;
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

	f = fopen(name, "rb");
	/* FIXME: Return error code so caller can report */
	HANDLE_ERROR(!f, fprintf(stderr, "File not found: '%s'", name));
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

static int compile_file(struct cstate *state, const char *name, int final)
{
	int result;
	char *input;
	const char *old_input;
	const char *old_filename;
	lex_state old_lex_state;

	assert(state != NULL);

	old_input = state->input;
	old_filename = state->filename;
	old_lex_state = state->lex_state;

	if (read_file(name, &input))
		return 1;
	state->input = input;
	state->lex_state = lex_state_new(name);
	state->filename = name;

	result = compile(state, final);

	state->input = old_input;
	state->filename = old_filename;
	state->lex_state = old_lex_state;

	free(input);
	return result;
}

int cb_compile_file(const char *name, struct bytecode **bc_out)
{
	int result;
	struct cstate state = cstate_default();

	result = compile_file(&state, name, 1);
	*bc_out = state.bytecode;

	return result;
}
