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
#include "cb_util.h"
#include "code.h"
#include "compiler.h"
#include "constant.h"
#include "eval.h"
#include "hashmap.h"
#include "module.h"
#include "opcode.h"
#include "struct.h"

#include "disassemble.h"

#define INITIAL_BYTECODE_SIZE 32
#define LENGTH(ARR) (sizeof(ARR) / sizeof(ARR[0]))
#define VALID_ESCAPES "nrt\"'"
#define STRUCT_MAX_FIELDS 64

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

struct lex_state {
	size_t name;
	size_t current_line, current_column;
	const char *ptr;
};

static void lex_state_init(struct lex_state *state, size_t name)
{
	state->name = name;
	state->current_line = 1;
	state->current_column = 1;
	state->ptr = NULL;
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
static int next_token(struct lex_state *state, const char *input,
		struct token *dest)
{
	const char *start_of_token;
	int is_double, c;
	unsigned i, len;

	assert(state != NULL);
	assert(input != NULL);
	assert(dest != NULL);
	if (state->ptr == NULL) {
		state->current_line = state->current_column = 1;
		state->ptr = input;
	}

/* Print an error message and location data */
#define ERROR(message, ...) do { \
		cb_str __name = cb_agent_get_string(state->name); \
		fprintf(stderr, "Error in %s at %zu:%zu: " message "\n", \
			cb_strptr(&__name), \
				state->current_line, \
				state->current_column, \
			##__VA_ARGS__); \
	} while (0)
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
	ssize_t name;
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

static struct scope *scope_new()
{
	return calloc(1, sizeof(struct scope));
}

static void scope_free(struct scope *s)
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

	FREE_LIST(s->locals);
	FREE_LIST(s->upvalues);

#undef FREE_LIST

	free(s);
}

static size_t scope_add_binding(struct scope *s, ssize_t name, int upvalue)
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
			if ((size_t) binding->name == name) \
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

static CB_INLINE size_t tok_len(struct token *tok)
{
	return tok->end - tok->start;
}

struct pending_address {
	struct pending_address *next;
	size_t label, address;
	unsigned arity, argno;
};

struct cb_bytecode {
	cb_instruction *code;
	size_t size, len;

	ssize_t *label_addresses;
	size_t label_addr_size, label_addr_len;

	struct pending_address *pending_addresses;
};

static struct cb_bytecode *cb_bytecode_new()
{
	struct cb_bytecode *bc;

	bc = malloc(sizeof(struct cb_bytecode));

	bc->code = NULL;
	bc->size = bc->len = 0;
	bc->label_addresses = NULL;
	bc->label_addr_size = bc->label_addr_len = 0;
	bc->pending_addresses = NULL;

	return bc;
}

static CB_INLINE size_t cb_bytecode_len(struct cb_bytecode *bc)
{
	return bc->len;
}

static void cb_bytecode_free(struct cb_bytecode *bc)
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

static size_t bytecode_label(struct cb_bytecode *bc)
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

static void bytecode_update_size_t(struct cb_bytecode *bc, size_t idx,
		size_t value)
{
	bc->code[idx] = value;
}

static void bytecode_mark_label(struct cb_bytecode *bc, size_t label)
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
			union cb_op_encoding op;
			op.as_size_t = bc->code[tmp->address];
			if (tmp->arity == 1) {
				op.unary.arg = bc->len;
			} else  {
				if (tmp->argno == 0)
					op.binary.arg1 = bc->len;
				else
					op.binary.arg2 = bc->len;
			}
			bytecode_update_size_t(bc, tmp->address, op.as_size_t);

			*prev = current;
			free(tmp);
		} else {
			prev = &tmp->next;
		}
	}
}

static void bytecode_push(struct cb_bytecode *bc, cb_instruction byte)
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

static size_t bytecode_address_of(struct cb_bytecode *bc, size_t label,
		unsigned arity, unsigned argno)
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
		addr->arity = arity;
		addr->argno = argno;
		bc->pending_addresses = addr;
	} else {
		value = bc->label_addresses[label];
	}

	return value;
}

static cb_instruction *bytecode_finalize(struct cb_bytecode *bc, size_t *len)
{
	if (bc->pending_addresses != NULL) {
		fprintf(stderr, "Missing label\n");
		return NULL;
	}

	if (bc->label_addresses) {
		free(bc->label_addresses);
		bc->label_addresses = NULL;
	}
	*len = bc->len;
	cb_instruction *code = realloc(bc->code, bc->len * sizeof(cb_instruction));
	bc->code = NULL;
	bc->label_addr_size = bc->label_addr_len = 0;

	return code;
}

struct parse_state {
	const char *input;
	int did_peek;
	struct token peeked;
	struct lex_state lex_state;
};

struct module_state {
	cb_modspec *spec;
	cb_hashmap *imported;
};

struct function_state {
	size_t name;
	struct scope *scope;
	struct function_state *parent;
	size_t *free_variables,
	       free_var_len,
	       free_var_size;
	int arity;
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

struct loc {
	size_t start, end;
	unsigned line, column;
};

struct cstate {
	struct parse_state *parse_state;
	struct module_state *module_state;
	struct function_state *function_state;
	struct loop_state *loop_state;
	struct cb_bytecode *bytecode;

	struct cb_const *consts;
	size_t consts_len, consts_size;

	struct loc *loc;
	size_t loc_len, loc_size;
};

struct loop_state {
	size_t continue_label;
	size_t break_label;
};

static struct parse_state *parse_state_new(const char *input, size_t name)
{
	struct parse_state *ps;

	ps = malloc(sizeof(struct parse_state));
	ps->input = input;
	ps->did_peek = 0;
	lex_state_init(&ps->lex_state, name);

	return ps;
}

static void parse_state_free(struct parse_state *ps)
{
	free(ps);
}

static struct module_state *module_state_new(cb_modspec *spec)
{
	struct module_state *state;

	state = malloc(sizeof(struct module_state));
	state->spec = spec;
	state->imported = cb_hashmap_new();

	return state;
}

static void module_state_free(struct module_state *state)
{
	cb_hashmap_free(state->imported);
	free(state);
}

static void cstate_init(struct cstate *const state, const char *input,
		size_t name)
{
	state->parse_state = parse_state_new(input, name);
	state->module_state = NULL;
	state->function_state = NULL;
	state->loop_state = NULL;
	state->bytecode = cb_bytecode_new();
	state->consts = NULL;
	state->consts_len = state->consts_size = 0;
	state->loc = NULL;
	state->loc_len = state->loc_size = 0;
}

static void cstate_deinit(const struct cstate *state)
{
	if (state->parse_state)
		parse_state_free(state->parse_state);
	if (state->bytecode)
		cb_bytecode_free(state->bytecode);
	if (state->consts)
		free(state->consts);
	if (state->loc)
		free(state->loc);
}

static void cstate_inherit(struct cstate *dest, const struct cstate *src)
{
	dest->parse_state = src->parse_state;
	dest->module_state = src->module_state;
	dest->function_state = NULL;
	dest->loop_state = NULL;
	dest->bytecode = cb_bytecode_new();
	dest->consts = NULL;
	dest->consts_len = dest->consts_size = 0;
	dest->loc = NULL;
	dest->loc_len = dest->loc_size = 0;
}

static size_t cstate_add_const(struct cstate *state, struct cb_const cst)
{
	if (state->consts_len >= state->consts_size) {
		if (state->consts_size == 0)
			state->consts_size = 4;
		else
			state->consts_size <<= 1;
		state->consts = realloc(state->consts,
				state->consts_size * sizeof(struct cb_const));
	}
	state->consts[state->consts_len] = cst;
	return state->consts_len++;
}

static int cstate_is_global(struct cstate *s)
{
	return !s->function_state;
}

static void cstate_push_loc(struct cstate *s, unsigned line, unsigned column)
{
	struct loc loc;
	loc.line = line;
	loc.column = column;
	loc.start = s->bytecode->len;

	if (s->loc_len == s->loc_size) {
		if (s->loc_size == 0)
			s->loc_size = 4;
		else
			s->loc_size <<= 1;
		s->loc = realloc(s->loc, s->loc_size * sizeof(struct loc));
	}
	if (s->loc_len != 0)
		s->loc[s->loc_len - 1].end = s->bytecode->len;
	s->loc[s->loc_len++] = loc;
}

static int resolve_binding(struct cstate *s, size_t name, struct binding *out)
{
	struct binding *binding;
	struct function_state *fstate;
	unsigned i;
	int found;

	/* i.e. we're in the global scope */
	if (cstate_is_global(s))
		return 0;

	fstate = s->function_state;
	binding = scope_get_binding(fstate->scope, name);
	if (binding) {
		*out = *binding;
		return 1;
	}
	if (scope_parent_has_binding(fstate->scope, name)) {
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
			out->index = s->function_state->scope->num_upvalues
				+ s->function_state->free_var_len;
			fstate_add_freevar(fstate, name);
		}
		return 1;
	}

	return 0;
}

static CB_INLINE const char *tok_start(struct cstate *state, struct token *tok)
{
	return state->parse_state->input + tok->start;
}

static CB_INLINE size_t intern_ident(struct cstate *state, struct token *tok)
{
	assert(tok->type == TOK_IDENT);
	return cb_agent_intern_string(tok_start(state, tok), tok_len(tok));
}

static struct token *peek(struct parse_state *state, int *ok)
{
	if (state->did_peek) {
		*ok = 1;
		return &state->peeked;
	}
	state->did_peek = 1;
	*ok = !next_token(&state->lex_state, state->input, &state->peeked);
	return &state->peeked;
}

static struct token next(struct parse_state *state, int *ok)
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
		_tok = peek(state->parse_state, &_ok); \
		if (!_ok) \
			return 1; \
		_tok; \
	})
#define NEXT() ({ \
		int _ok; \
		struct token _tok = next(state->parse_state, &_ok); \
		/* FIXME: this isn't always the most ideal spot */ \
		cstate_push_loc(state, _tok.line, _tok.column); \
		if (!_ok) \
			return 1; \
		_tok; \
	})
#define MATCH_P(TYPE) ({ \
		struct token *tok = PEEK(); \
		tok && tok->type != TOK_EOF && tok->type == (TYPE); \
	})
#define MAYBE_CONSUME(TYPE) (MATCH_P(TYPE) ? (NEXT(), 1) : 0)
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
		cb_str __name; \
		if (TOK) { \
			ERROR_AT(*(TOK), MSG, ##__VA_ARGS__); \
		} else { \
			__name = cb_agent_get_string( \
					state->parse_state->lex_state.name); \
			fprintf(stderr, "Error in %s at EOF: " MSG "\n", \
				cb_strptr(&__name), ##__VA_ARGS__); \
		} \
	})
#define ERROR_AT(TOK, MSG, ...) ({ \
		cb_str __name = cb_agent_get_string( \
				state->parse_state->lex_state.name); \
		fprintf(stderr, "Error in %s at %zu:%zu: " MSG "\n", \
				cb_strptr(&__name), (TOK).line, \
				(TOK).column, ##__VA_ARGS__); \
	})

#define OP(X) ({ \
		union cb_op_encoding _op; \
		_op.unary.op = (X); \
		_op.as_size_t; \
	})
#define OP1(X, ARG) ({ \
		union cb_op_encoding _op; \
		_op.unary.op = (X); \
		_op.unary.arg = (ARG); \
		_op.as_size_t; \
	})
#define OP2(X, ARG1, ARG2) ({ \
		union cb_op_encoding _op; \
		_op.binary.op = (X); \
		_op.binary.arg1 = (ARG1); \
		_op.binary.arg2 = (ARG2); \
		_op.as_size_t; \
	})

#define APPEND(B) (bytecode_push(state->bytecode, OP(B)))
#define APPEND1(A, B) (bytecode_push(state->bytecode, OP1(A, B)))
#define APPEND2(A, B, C) (bytecode_push(state->bytecode, OP2(A, B, C)))
#define LABEL() (bytecode_label(state->bytecode))
#define MARK(L) (bytecode_mark_label(state->bytecode, (L)))
#define ADDR_OF(L, ARITY, ARGNO) \
	(bytecode_address_of(state->bytecode, (L), (ARITY), (ARGNO)))

/* Propagate errors */
#define X(V) ({ if (V) return 1; })

static int compile_statement(struct cstate *);
static int compile_let_statement(struct cstate *, int export);
static int compile_function_statement(struct cstate *, int export);
static int compile_if_statement(struct cstate *);
static int compile_for_statement(struct cstate *);
static int compile_while_statement(struct cstate *);
static int compile_break_statement(struct cstate *);
static int compile_continue_statement(struct cstate *);
static int compile_return_statement(struct cstate *);
static int compile_export_statement(struct cstate *);
static int compile_import_statement(struct cstate *);
static int compile_struct_statement(struct cstate *, int export);
static int compile_expression_statement(struct cstate *);

static int compile_expression(struct cstate *);

static struct cb_code *create_code(struct cstate *state)
{
	struct cb_code *code;
	ssize_t current;
	size_t i;

	code = cb_code_new();

	/* Calculate the maximum stack size this code fragment requires, excluding
	   arguments */
	code->stack_size = 0;
	if (!cstate_is_global(state))
		code->stack_size += state->function_state->scope->num_locals;

	/* NOTE: this assumes that a loop cannot have positive stack effect */
	current = code->stack_size;
	i = 0;
	for (i = 0; i < cb_bytecode_len(state->bytecode); i++) {
		cb_instruction instr = state->bytecode->code[i];
		current += cb_opcode_stack_effect(instr);
#ifndef NDEBUG
		if (current < 0) {
			for (size_t j = 0; j < cb_bytecode_len(state->bytecode); j++) {
				assert(!cb_disassemble_one(state->bytecode->code[j], j));
			}
			abort();
		}
#endif
		if (current > code->stack_size)
			code->stack_size = current;
	}

	if (state->loc_len != 0)
		state->loc[state->loc_len - 1].end = state->bytecode->len;
	code->loc = malloc(sizeof(struct cb_loc) * state->loc_len);
	unsigned loc_len = 0;
	for (unsigned i = 0; i < state->loc_len; i++) {
		struct loc *in = &state->loc[i];
		if (in->start == in->end)
			continue;

		struct cb_loc *out = &code->loc[loc_len++];
		out->run = in->end - in->start;
		out->line = in->line;
		out->column = in->column;
	}
	code->loc = realloc(code->loc, sizeof(struct cb_loc) * loc_len);

	code->bytecode = bytecode_finalize(state->bytecode, &code->bytecode_len);
	code->nconsts = state->consts_len;
	code->const_pool = realloc(state->consts,
			state->consts_len * sizeof(struct cb_const));
	code->modspec = state->module_state->spec;
	code->ic = calloc(code->bytecode_len, sizeof(union cb_inline_cache));

	/* ownership taken */
	state->consts = NULL;
	cb_bytecode_free(state->bytecode);
	state->bytecode = NULL;

	if (!cstate_is_global(state)) {
		code->nlocals = state->function_state->scope->num_locals;
		code->nupvalues = state->function_state->free_var_len;
	} else {
		/* TODO: use locals for global scope */
		code->nupvalues = code->nlocals = 0;
	}

	return code;
}

static int compile(struct cstate *state, enum token_type end_tok)
{
	struct token *tok;

	while ((tok = PEEK()) && tok->type != end_tok) {
		if (compile_statement(state))
			return 1;
	}

	EXPECT(end_tok);

	return 0;
}

static struct cb_code *compile_into_module(struct cstate *state,
		cb_modspec *modspec)
{
	struct cb_code *code;

	state->module_state = module_state_new(modspec);

	if (compile(state, TOK_EOF))
		return NULL;

	APPEND(OP_HALT);

	code = create_code(state);

	module_state_free(state->module_state);
	state->module_state = NULL;

	return code;
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
		X(compile_struct_statement(state, 0));
		break;
	default:
		X(compile_expression_statement(state));
		break;
	}

	return 0;
}

static void export_name(struct cstate *state, size_t name)
{
	/* Can't export something from inside a function */
	assert(cstate_is_global(state));

	cb_modspec_add_export(state->module_state->spec, name);
}

static int declare_name(struct cstate *state, size_t name_id, int export)
{
	size_t binding_id;

	if (cstate_is_global(state)) {
		/* FIXME: let DECLARE_GLOBAL also assign the value */
		APPEND1(OP_DECLARE_GLOBAL, name_id);
		APPEND1(OP_STORE_GLOBAL, name_id);
		if (export)
			export_name(state, name_id);
		APPEND(OP_POP);
	} else {
		binding_id = scope_add_binding(state->function_state->scope,
				name_id, 0);
		APPEND1(OP_STORE_LOCAL, binding_id);
		APPEND(OP_POP);
	}

	return 0;
}

static size_t const_int(struct cstate *state, int64_t value)
{
	struct cb_const c;
	c.type = CB_CONST_INT;
	c.val.as_int = value;
	return cstate_add_const(state, c);
}

static size_t const_double(struct cstate *state, double value)
{
	struct cb_const c;
	c.type = CB_CONST_DOUBLE;
	c.val.as_double = value;
	return cstate_add_const(state, c);
}

#define MAX_DESTRUCTURE_ASSIGN 32

enum assignment_pattern_type {
	PATTERN_IDENT,
	PATTERN_ARRAY,
	PATTERN_STRUCT
};

struct assignment_pattern {
	enum assignment_pattern_type type;
	union {
		size_t ident;
		struct {
			size_t nassigns,
			       name_ids[MAX_DESTRUCTURE_ASSIGN];
		} array;
		struct {
			size_t nassigns,
			       name_ids[MAX_DESTRUCTURE_ASSIGN];
			ssize_t aliases[MAX_DESTRUCTURE_ASSIGN];
		} struct_;
	} p;
};

static int parse_assignment_pattern(struct cstate *state,
		struct assignment_pattern *pattern, int dry_run)
{
#define _EXPECT(T) ({ \
		if (dry_run && !MATCH_P(T)) \
			return 1; \
		EXPECT(T); \
	})

	struct token tok;
	size_t nassigns;

	if (MATCH_P(TOK_IDENT)) {
		pattern->type = PATTERN_IDENT;
		tok = _EXPECT(TOK_IDENT);
		pattern->p.ident = intern_ident(state, &tok);
	} else if (MATCH_P(TOK_LEFT_BRACKET)) {
		nassigns = 0;
		_EXPECT(TOK_LEFT_BRACKET);
		while (!MAYBE_CONSUME(TOK_RIGHT_BRACKET)) {
			if (nassigns)
				_EXPECT(TOK_COMMA);
			if (MAYBE_CONSUME(TOK_RIGHT_BRACKET))
				break;
			tok = _EXPECT(TOK_IDENT);
			pattern->p.array.name_ids[nassigns++] =
					intern_ident(state, &tok);
			if (nassigns == MAX_DESTRUCTURE_ASSIGN) {
				ERROR_AT(tok, "Too many destructuring assignment targets");
				return 1;
			}
		}
		pattern->type = PATTERN_ARRAY;
		pattern->p.array.nassigns = nassigns;
	} else if (MATCH_P(TOK_LEFT_BRACE)) {
		nassigns = 0;
		EXPECT(TOK_LEFT_BRACE);
		while (!MAYBE_CONSUME(TOK_RIGHT_BRACE)) {
			if (nassigns)
				EXPECT(TOK_COMMA);
			if (MAYBE_CONSUME(TOK_RIGHT_BRACKET))
				break;
			tok = EXPECT(TOK_IDENT);
			pattern->p.struct_.name_ids[nassigns++] =
					intern_ident(state, &tok);
			if (nassigns == MAX_DESTRUCTURE_ASSIGN) {
				ERROR_AT(tok, "Too many destructuring assignment targets");
				return 1;
			}
			if (MAYBE_CONSUME(TOK_COLON)) {
				tok = EXPECT(TOK_IDENT);
				pattern->p.struct_.aliases[nassigns - 1] =
						intern_ident(state, &tok);
			} else {
				pattern->p.struct_.aliases[nassigns - 1] = -1;
			}
		}
		pattern->type = PATTERN_STRUCT;
		pattern->p.struct_.nassigns = nassigns;
	}

	return 0;

#undef _EXPECT
}

/* Expects the source value to be on the stack already, and does not leave the
   value on the stack. */
static int compile_assignment_pattern(struct cstate *state,
		struct assignment_pattern *pat, int export)
{
	size_t i;

	switch (pat->type) {
	case PATTERN_IDENT:
		X(declare_name(state, pat->p.ident, export));
		break;
	case PATTERN_ARRAY:
		for (i = 0; i < pat->p.array.nassigns; i += 1) {
			APPEND(OP_DUP);
			size_t const_id = const_int(state, i);
			APPEND1(OP_LOAD_CONST, const_id);
			APPEND(OP_ARRAY_GET);
			X(declare_name(state, pat->p.array.name_ids[i], export));
		}
		APPEND(OP_POP);
		break;
	case PATTERN_STRUCT:
		for (i = 0; i < pat->p.struct_.nassigns; i += 1) {
			APPEND(OP_DUP);
			APPEND1(OP_LOAD_STRUCT, pat->p.struct_.name_ids[i]);
			X(declare_name(state,
				pat->p.struct_.aliases[i] < 0
					? pat->p.struct_.name_ids[i]
					: (size_t) pat->p.struct_.aliases[i],
				export));
		}
		APPEND(OP_POP);
		break;
	default:
		fprintf(stderr, "Bad assignment pattern type");
		return 1;
	}

	return 0;
}

#undef MAX_DESTRUCTURE_ASSIGN

static int compile_let_statement(struct cstate *state, int export)
{
	struct assignment_pattern pat;

	EXPECT(TOK_LET);

	do {
		X(parse_assignment_pattern(state, &pat, 0));
		if (MATCH_P(TOK_EQUAL)) {
			EXPECT(TOK_EQUAL);
			X(compile_expression(state));
		} else {
			APPEND(OP_CONST_NULL);
		}
		X(compile_assignment_pattern(state, &pat, export));
	} while (MAYBE_CONSUME(TOK_COMMA));
	EXPECT(TOK_SEMICOLON);

	return 0;
}

static int compile_const_function(struct cstate *state, size_t **free_vars_out,
		struct cb_const_user_function *func_out, size_t *binding_id_out)
{
	struct cstate inner_state;
	struct function_state inner_fn_state;
	struct token name;
	struct assignment_pattern param_patterns[CB_MAX_PARAMS];
	size_t bindings[CB_MAX_PARAMS];
	size_t name_id,
	       binding_id,
	       num_params,
	       next_opt_param,
	       allocate_locals_pos,
	       num_opt_params;
	int is_first_param;

#define APPEND_INNER(B) (bytecode_push(inner_state.bytecode, OP(B)))
#define APPEND1_INNER(A, B) (bytecode_push(inner_state.bytecode, OP1(A, B)))
#define APPEND2_INNER(A, B, C) (bytecode_push(inner_state.bytecode, OP2(A, B, C)))
#define LABEL_INNER() (bytecode_label(inner_state.bytecode))
#define MARK_INNER(L) (bytecode_mark_label(inner_state.bytecode, (L)))
#define ADDR_OF_INNER(L, ARITY, ARGNO) \
	(bytecode_address_of(inner_state.bytecode, (L), (ARITY), (ARGNO)))
#define UPDATE_INNER(IDX, VAL) (bytecode_update_size_t(inner_state.bytecode, \
			(IDX), (VAL)))

	cstate_inherit(&inner_state, state);
	inner_state.function_state = &inner_fn_state;
	inner_fn_state.parent = state->function_state;
	inner_fn_state.scope = scope_new();
	inner_fn_state.free_variables = NULL;
	inner_fn_state.free_var_len = inner_fn_state.free_var_size = 0;
	if (state->function_state)
		inner_fn_state.scope->parent = state->function_state->scope;

	EXPECT(TOK_FUNCTION);
	if (MATCH_P(TOK_IDENT)) {
		name = EXPECT(TOK_IDENT);
		name_id = intern_ident(state, &name);
		if (!cstate_is_global(state)) {
			/* Add the function to the current scope so recursive
			   calls refer to the right variable */
			binding_id = scope_add_binding(
					state->function_state->scope,
					name_id, 0);
			if (binding_id_out)
				*binding_id_out = binding_id;
		}
	} else {
		name_id = cb_agent_intern_string("<anonymous>", 11);
		if (binding_id_out)
			*binding_id_out = -1;
	}

	num_params = 0;
	is_first_param = 1;
	num_opt_params = 0;
	next_opt_param = LABEL_INNER();

	EXPECT(TOK_LEFT_PAREN);
	while (!MATCH_P(TOK_RIGHT_PAREN)) {
		if (is_first_param) {
			is_first_param = 0;
		} else {
			EXPECT(TOK_COMMA);
			/* support trailing comma */
			if (MATCH_P(TOK_RIGHT_PAREN))
				break;
		}
		bindings[num_params] = scope_add_binding(inner_fn_state.scope,
				-1, 0);
		if (parse_assignment_pattern(&inner_state,
					&param_patterns[num_params++], 0))
			return -1;

		if (MAYBE_CONSUME(TOK_EQUAL)) {
			num_opt_params += 1;
			MARK_INNER(next_opt_param);
			next_opt_param = LABEL_INNER();
			APPEND2_INNER(OP_APPLY_DEFAULT_ARG, num_params - 1,
					ADDR_OF_INNER(next_opt_param, 2, 1));
			/* The default value is just left on the stack since
			   that's where the local variable will be located */
			if (compile_expression(&inner_state))
				return -1;
		} else if (num_opt_params) {
			EXPECT(TOK_EQUAL);
		}
	}
	EXPECT(TOK_RIGHT_PAREN);
	EXPECT(TOK_LEFT_BRACE);

	inner_fn_state.arity = num_params;

	MARK_INNER(next_opt_param);

	allocate_locals_pos = cb_bytecode_len(inner_state.bytecode);
	APPEND1_INNER(OP_ALLOCATE_LOCALS, 0);

	for (ssize_t i = num_params - 1; i >= 0; i -= 1) {
		/* Avoid copying all arguments that aren't patterns */
		if (param_patterns[i].type == PATTERN_IDENT) {
			// FIXME
			for (struct binding *b = inner_fn_state.scope->locals;
					b; b = b->next) {
				if (b->index == bindings[i]) {
					b->name = param_patterns[i].p.ident;
					break;
				}
			}
			continue;
		}
		APPEND1_INNER(OP_LOAD_LOCAL, bindings[i]);
		if (compile_assignment_pattern(&inner_state,
					&param_patterns[i], 0))
			return -1;
	}

	if (compile(&inner_state, TOK_RIGHT_BRACE))
		return -1;

	/* potentially an extra return, but better safe than sorry */
	APPEND_INNER(OP_CONST_NULL);
	APPEND_INNER(OP_RETURN);

	/* Make room on the stack for the local variables. Arguments will
	 * already be on the stack, so we don't need to allocate more room for
	 * them. */
	UPDATE_INNER(allocate_locals_pos, OP1(OP_ALLOCATE_LOCALS,
				inner_fn_state.scope->num_locals - num_params));

	func_out->code = create_code(&inner_state);
	func_out->name = name_id;
	func_out->arity = num_params;
	func_out->num_opt_params = num_opt_params;

	scope_free(inner_fn_state.scope);

	inner_state.module_state = NULL;
	inner_state.parse_state = NULL;
	cstate_deinit(&inner_state);

#undef APPEND_INNER
#undef APPEND1_INNER
#undef APPEND2_INNER
#undef LABEL_INNER
#undef MARK_INNER
#undef ADDR_OF_INNER
#undef UPDATE_INNER

	if (free_vars_out)
		*free_vars_out = inner_fn_state.free_variables;

	return inner_fn_state.free_var_len;
}

static int compile_function(struct cstate *state, size_t *name_out)
{
	ssize_t free_var_len;
	size_t free_var, *free_vars;
	size_t i, j, const_id, binding_id;
	struct cb_const_user_function *func;
	struct cb_const func_const;
	struct binding *binding;

	func = malloc(sizeof(struct cb_const_user_function));
	free_var_len = compile_const_function(state, &free_vars, func,
			&binding_id);
	if (free_var_len < 0) {
		free(func);
		return 1;
	}

	if (name_out != NULL)
		*name_out = func->name;

	func_const.type = CB_CONST_FUNCTION;
	func_const.val.as_function = func;
	const_id = cstate_add_const(state, func_const);
	APPEND1(OP_LOAD_CONST, const_id);

	if (!cstate_is_global(state) && binding_id != (size_t) -1) {
		APPEND1(OP_STORE_LOCAL, binding_id);
	}

	for (j = i = 0; i < (size_t) free_var_len; i += 1) {
		free_var = free_vars[i];
		binding = scope_get_binding(state->function_state->scope,
				free_var);
		if (binding != NULL) {
			if (binding->is_upvalue)
				APPEND2(OP_BIND_UPVALUE, i, binding->index);
			else
				APPEND2(OP_BIND_LOCAL, i, binding->index);
		} else if (scope_parent_has_binding(state->function_state->scope,
					free_var)) {
			assert(state->function_state != NULL);
			fstate_add_freevar(state->function_state, free_var);
			APPEND2(OP_BIND_UPVALUE, i,
					state->function_state->scope->num_upvalues + j);
			j += 1;
		}
	}

	if (free_vars != NULL)
		free(free_vars);

	return 0;
}

static int compile_function_statement(struct cstate *state, int export)
{
	size_t name;

	X(compile_function(state, &name));
	/* FIXME: all functions have names, so this is declaring anonymous
	   functions as "<anonymous>" */
	if (name != (size_t) -1)
		declare_name(state, name, export);

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

	APPEND1(OP_JUMP_IF_FALSE, ADDR_OF(else_label, 1, 0));

	EXPECT(TOK_RIGHT_PAREN);
	EXPECT(TOK_LEFT_BRACE);

	while (!MATCH_P(TOK_RIGHT_BRACE))
		X(compile_statement(state));

	EXPECT(TOK_RIGHT_BRACE);

	APPEND1(OP_JUMP, ADDR_OF(end_label, 1, 0));
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
		APPEND1(OP_JUMP_IF_FALSE, ADDR_OF(end_label, 1, 0));
	}
	EXPECT(TOK_SEMICOLON);

	APPEND1(OP_JUMP, ADDR_OF(body_label, 1, 0));

	MARK(increment_label);

	/* if there is an increment */
	if (!MATCH_P(TOK_RIGHT_PAREN)) {
		X(compile_expression(state));
		APPEND(OP_POP);
	}
	EXPECT(TOK_RIGHT_PAREN);
	EXPECT(TOK_LEFT_BRACE);

	APPEND1(OP_JUMP, ADDR_OF(start_label, 1, 0));
	MARK(body_label);

	old_lstate = state->loop_state;
	state->loop_state = &lstate;

	while (!MATCH_P(TOK_RIGHT_BRACE))
		X(compile_statement(state));

	EXPECT(TOK_RIGHT_BRACE);
	APPEND1(OP_JUMP, ADDR_OF(increment_label, 1, 0));
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
	APPEND1(OP_JUMP_IF_FALSE, ADDR_OF(end_label, 1, 0));

	old_lstate = state->loop_state;
	state->loop_state = &lstate;

	EXPECT(TOK_RIGHT_PAREN);
	EXPECT(TOK_LEFT_BRACE);
	while (!MATCH_P(TOK_RIGHT_BRACE))
		X(compile_statement(state));
	EXPECT(TOK_RIGHT_BRACE);

	APPEND1(OP_JUMP, ADDR_OF(start_label, 1, 0));
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

	APPEND1(OP_JUMP, ADDR_OF(state->loop_state->break_label, 1, 0));

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

	APPEND1(OP_JUMP, ADDR_OF(state->loop_state->continue_label, 1, 0));

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

	tok = EXPECT(TOK_EXPORT);

	if (!cstate_is_global(state)) {
		ERROR_AT(tok, "Can only export from global scope");
		return 1;
	}

	if (MATCH_P(TOK_LET)) {
		X(compile_let_statement(state, 1));
	} else if (MATCH_P(TOK_FUNCTION)) {
		X(compile_function_statement(state, 1));
	} else if (MATCH_P(TOK_STRUCT)) {
		X(compile_struct_statement(state, 1));
	} else {
		ERROR_AT_P(PEEK(), "Can only export function, let, and struct declarations");
		return 1;
	}

	return 0;
}

static int compile_import_statement(struct cstate *state)
{
	struct token tok, ident;
	char *filename;
	cb_str modname_str;
	size_t modname;
	const struct cb_builtin_module_spec *builtin;
	FILE *f;
	cb_modspec *imported_modspec;
	struct cb_code *imported_code;
	struct cb_const_module *const_mod;
	struct cb_const module_const;
	size_t const_id;

	tok = EXPECT(TOK_IMPORT);
	if (!cstate_is_global(state)) {
		ERROR_AT(tok, "Import must be at top level");
		return 1;
	}

	ident = EXPECT(TOK_IDENT);
	EXPECT(TOK_SEMICOLON);

	modname = intern_ident(state, &ident);
	modname_str = cb_agent_get_string(modname);
	builtin = NULL;

	if (cb_hashmap_get(state->module_state->imported, modname, NULL))
		return 0;

	/* If the import name is one of the names is one of the builtin
	 * modules, just add that builtin module to state->imported */
	for (size_t i = 0; i < cb_builtin_module_count; i += 1) {
		size_t builtin_name_len = strlen(cb_builtin_modules[i].name);
		if (cb_str_eq_cstr(modname_str, cb_builtin_modules[i].name,
				builtin_name_len)) {
			builtin = &cb_builtin_modules[i];
			break;
		}
	}

	if (!builtin) {
		cb_str _name = cb_agent_get_string(
				state->parse_state->lex_state.name);
		/* FIXME: this sucks */
		if (cb_strlen(cb_agent_get_string(
					state->parse_state->lex_state.name))
				== sizeof("<script>") - 1
				&& !strncmp("<script>", cb_strptr(&_name),
					sizeof("<script>") - 1)) {
			filename = malloc(1);
			*filename = 0;
		} else {
			char *real_path = realpath(cb_strptr(&_name), NULL);
			const char *dir_name = dirname(real_path);
			size_t len = strlen(dir_name);
			filename = malloc(len + 1);
			filename[len] = 0;
			memcpy(filename, dir_name, strlen(dir_name));
			free(real_path);
		}

		f = cb_agent_resolve_import(modname_str, filename, NULL);
		free(filename);
		if (!f)
			goto error;

		/* FIXME: Only compile a module once */
		imported_modspec = cb_modspec_new(modname);
		cb_agent_add_modspec(imported_modspec);
		imported_code = cb_compile_file(imported_modspec, f);
		if (!imported_code)
			return 1;
		const_mod = malloc(sizeof(struct cb_const_module));
		const_mod->spec = imported_modspec;
		const_mod->code = imported_code;
		module_const.type = CB_CONST_MODULE;
		module_const.val.as_module = const_mod;
		const_id = cstate_add_const(state, module_const);

		APPEND1(OP_IMPORT_MODULE, const_id);
	}

	cb_hashmap_set(state->module_state->imported, modname,
			(struct cb_value) { .type = CB_VALUE_NULL });

	return 0;

error:
	return 1;
}

static ssize_t compile_struct_decl(struct cstate *state, size_t *name_out,
		int has_values_inline, struct cb_struct_spec **spec_out)
{
	struct token name, field;
	size_t name_id, num_fields, binding_id;
	int first_field;
	int is_named;
	size_t fields[STRUCT_MAX_FIELDS];

	EXPECT(TOK_STRUCT);
	if (MATCH_P(TOK_IDENT)) {
		is_named = 1;
		name = EXPECT(TOK_IDENT);
		name_id = intern_ident(state, &name);
		if (name_out)
			*name_out = name_id;
	} else {
		is_named = 0;
		name_id = cb_agent_intern_string("<anonymous>", 11);
	}

	num_fields = 0;
	first_field = 1;

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
		field = EXPECT(TOK_IDENT);
		fields[num_fields++] = intern_ident(state, &field);
		if (num_fields == STRUCT_MAX_FIELDS) {
			ERROR_AT(field, "Too many struct fields");
			return -1;
		}
		if (has_values_inline) {
			EXPECT(TOK_EQUAL);
			if (compile_expression(state))
				return -1;
		}
	}
	EXPECT(TOK_RIGHT_BRACE);

	struct cb_struct_spec *spec = cb_struct_spec_new(name_id, num_fields);
	if (spec_out)
		*spec_out = spec;
	for (unsigned i = 0; i < num_fields; i++) {
		cb_struct_spec_set_field_name(spec, i, fields[i]);
	}
	struct cb_const const_spec;
	const_spec.type = CB_CONST_STRUCT_SPEC;
	const_spec.val.as_struct_spec = spec;
	size_t const_id = cstate_add_const(state, const_spec);

	if (is_named) {
		APPEND1(OP_LOAD_CONST, const_id);
		if (cstate_is_global(state)) {
			APPEND1(OP_DECLARE_GLOBAL, name_id);
			APPEND1(OP_STORE_GLOBAL, name_id);
		} else {
			binding_id = scope_add_binding(state->function_state->scope,
					name_id, 0);
			APPEND1(OP_STORE_LOCAL, binding_id);
		}
	}

	return const_id;
}

static int compile_struct_statement(struct cstate *state, int export)
{
	size_t name_id;

	if (compile_struct_decl(state, &name_id, 0, NULL) < 0)
		return 1;

	if (export && cstate_is_global(state))
		export_name(state, name_id);
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
	case TOK_SLASH: return 11;
	case TOK_PERCENT: return 11;
	case TOK_STAR_STAR: return 12;

	case TOK_LEFT_PAREN:
	case TOK_LEFT_BRACKET:
	case TOK_COLON:
	case TOK_LEFT_BRACE:
		return 13;

	case TOK_DOT:
		return 14;

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
		size_t const_id = const_int(state, num);
		APPEND1(OP_LOAD_CONST, const_id);
	} else {
		ERROR_AT(tok, "Invalid integer literal");
		return 1;
	}

	return 0;
}

static int store_name(struct cstate *state, size_t name)
{
	struct binding binding;

	if (!resolve_binding(state, name, &binding)) {
		APPEND1(OP_STORE_GLOBAL, name);
	} else {
		if (binding.is_upvalue) {
			assert(state->function_state != NULL);
			APPEND1(OP_STORE_UPVALUE, binding.index);
		} else {
			APPEND1(OP_STORE_LOCAL, binding.index);
		}
	}

	return 0;
}

static int load_name(struct cstate *state, size_t name)
{
	struct binding binding;

	if (resolve_binding(state, name, &binding)) {
		if (binding.is_upvalue) {
			assert(state->function_state != NULL);
			APPEND1(OP_LOAD_UPVALUE, binding.index);
		} else {
			APPEND1(OP_LOAD_LOCAL, binding.index);
		}
	} else {
		APPEND1(OP_LOAD_GLOBAL, name);
	}

	return 0;
}

static int compile_identifier_expression(struct cstate *state)
{
	struct token tok, export;
	size_t name;
	int ok;
	cb_modspec *module;
	cb_str modname;

	tok = EXPECT(TOK_IDENT);
	name = intern_ident(state, &tok);

	if (MATCH_P(TOK_DOT)) {
		NEXT();
		export = EXPECT(TOK_IDENT);
		module = cb_agent_get_modspec_by_name(name);
		if (!module) {
			modname = cb_agent_get_string(
					intern_ident(state, &tok));
			ERROR_AT(tok, "No such module %s\n",
					cb_strptr(&modname));
			return 1;
		} else if (!cb_hashmap_get(state->module_state->imported, name,
					NULL)) {
			modname = cb_agent_get_string(
					intern_ident(state, &tok));
			ERROR_AT(tok, "Missing import for module %s\n",
					cb_strptr(&modname));
			return 1;
		}
		APPEND2(OP_LOAD_FROM_MODULE, cb_modspec_id(module),
				cb_modspec_get_export_id(module,
					intern_ident(state, &export), &ok));
		if (!ok) {
			modname = cb_agent_get_string(cb_modspec_name(module));
			cb_str export_name = cb_agent_get_string(
					intern_ident(state, &export));
			ERROR_AT(export, "Module %s has no export %s",
					cb_strptr(&modname),
					cb_strptr(&export_name));
			return 1;
		}
		return 0;
	}

	if (MATCH_P(TOK_EQUAL)) {
		NEXT();
		X(compile_expression(state));
		X(store_name(state, name));
	} else {
		X(load_name(state, name));
	}

	return 0;
}

static int compile_double_expression(struct cstate *state)
{
	struct token tok;
	char *buf;
	size_t len;
	double val;

	tok = EXPECT(TOK_DOUBLE);

	/* FIXME: find way to parse double from non-null-terminated string
	 * This allocation should not be necessary */
	len = tok_len(&tok);
	buf = malloc(len + 1);
	memcpy(buf, tok_start(state, &tok), len);
	buf[len] = 0;

	val = strtod(buf, NULL);
	free(buf);

	size_t const_id = const_double(state, val);
	APPEND1(OP_LOAD_CONST, const_id);

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
	size_t id, len, toklen, i;
	char *str;
	const char *lit;

	tok = EXPECT(TOK_STRING);
	toklen = tok_len(&tok) - 2;
	lit = tok_start(state, &tok) + 1;
	str = malloc(toklen + 1);

	for (i = len = 0; i < toklen; i += 1) {
		if (lit[i] != '\\') {
			str[len++] = lit[i];
			continue;
		}
		str[len++] = TRANSLATE_ESCAPE(lit[++i]);
	}
	str[len] = 0;

	id = cb_agent_intern_string(str, len);
	free(str);

	APPEND1(OP_CONST_STRING, id);

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

	APPEND1(OP_CONST_CHAR, c);

	return 0;
}

static int compile_parenthesized_expression(struct cstate *state)
{
	EXPECT(TOK_LEFT_PAREN);
	X(compile_expression(state));
	EXPECT(TOK_RIGHT_PAREN);

	return 0;
}

static int compile_struct_destructuring_assignment(struct cstate *state)
{
	struct assignment_pattern pat;
	size_t i, dest_name;

	X(parse_assignment_pattern(state, &pat, 0));
	assert(pat.type == PATTERN_STRUCT);

	EXPECT(TOK_EQUAL);
	X(compile_expression(state));

	for (i = 0; i < pat.p.struct_.nassigns; i += 1) {
		APPEND(OP_DUP);
		APPEND1(OP_LOAD_STRUCT, pat.p.struct_.name_ids[i]);
		dest_name = pat.p.struct_.aliases[i] < 0
			? pat.p.struct_.name_ids[i]
			: (size_t) pat.p.struct_.aliases[i];
		X(store_name(state, dest_name));
		APPEND(OP_POP);
	}

	return 0;
}

static int compile_array_expression(struct cstate *state)
{
	size_t num_elements, i;
	int first_elem;
	struct parse_state og_parse_state;
	struct assignment_pattern pat;

	og_parse_state = *state->parse_state;

	if (!parse_assignment_pattern(state, &pat, 1) && MATCH_P(TOK_EQUAL)) {
		assert(pat.type == PATTERN_ARRAY);
		EXPECT(TOK_EQUAL);

		X(compile_expression(state));
		for (i = 0; i < pat.p.array.nassigns; i += 1) {
			APPEND(OP_DUP);
			size_t const_id = const_int(state, i);
			APPEND1(OP_LOAD_CONST, const_id);
			APPEND(OP_ARRAY_GET);
			X(store_name(state, pat.p.array.name_ids[i]));
			APPEND(OP_POP);
		}
	} else {
		*state->parse_state = og_parse_state;
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
		APPEND1(OP_NEW_ARRAY_WITH_VALUES, num_elements);
	}

	return 0;
}

static int compile_anonymous_struct_literal(struct cstate *state)
{
	struct cb_struct_spec *spec;
	ssize_t const_id = compile_struct_decl(state, NULL, 1, &spec);
	if (const_id < 0)
		return 1;

	/* FIXME: add a new instruction like OP_ARRAY_FROM_VALUES */
	APPEND1(OP_LOAD_CONST, const_id);
	APPEND(OP_NEW_STRUCT);
	for (int i = spec->nfields - 1; i >= 0; i -= 1) {
		APPEND(OP_ROT_2);
		APPEND1(OP_ADD_STRUCT_FIELD, spec->fields[i]);
	}

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
		X(compile_anonymous_struct_literal(state));
		break;
	case TOK_LEFT_BRACE:
		X(compile_struct_destructuring_assignment(state));
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

	APPEND1(OP_CALL, num_args);

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
		APPEND1(OP_STORE_STRUCT, name);
	} else {
		APPEND1(OP_LOAD_STRUCT, name);
	}

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
		APPEND1(OP_ADD_STRUCT_FIELD, name);
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
		APPEND1(OP_JUMP_IF_FALSE, ADDR_OF(end_label, 1, 0));
		break;
	case TOK_PIPE_PIPE:
		APPEND1(OP_JUMP_IF_TRUE, ADDR_OF(end_label, 1, 0));
		break;
	default:
		abort(); /* unreachable */
	}

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

size_t name_from_path(const char *path)
{
	char *dup = strdup(path);
	char *fname;
	size_t i, name;

	fname = basename(dup);
	for (i = 0; fname[i] && fname[i] != '.'; i += 1);
	fname[i] = 0;

	name = cb_agent_intern_string(fname, i);
	free(dup);

	return name;
}

struct cb_code *cb_compile_string(cb_modspec *module, const char *source)
{
	struct cstate state;
	struct cb_code *code;

	cstate_init(&state, source, cb_modspec_name(module));
	code = compile_into_module(&state, module);
	cstate_deinit(&state);

	return code;
}

struct cb_code *cb_compile_file(cb_modspec *module, FILE *f)
{
	char *input;
	struct cb_code *code;

	if (read_file(f, &input))
		return NULL;

	code = cb_compile_string(module, input);
	free(input);

	return code;
}