#ifndef cb_compiler_h
#define cb_compiler_h

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "module.h"
#include "str.h"

typedef struct bytecode cb_bytecode;
typedef uint32_t cb_instruction;
typedef struct cstate cb_compile_state;

int cb_compile_file(const char *name, const char *path, cb_bytecode **bc_out);
int cb_compile_module(cb_bytecode *bc, cb_str name, FILE *f,
		const char *path);
int cb_compiler_resume(cb_compile_state *state);

cb_compile_state *cb_compile_state_new(const char *name, cb_modspec *modspec,
		cb_bytecode *bc);
void cb_compile_state_free(cb_compile_state *state);
void cb_compile_state_reset(cb_compile_state *st, const char *input,
		cb_modspec *modspec);

cb_bytecode *cb_bytecode_new(void);
cb_instruction cb_bytecode_get(cb_bytecode *bc, size_t idx);
size_t cb_bytecode_len(cb_bytecode *bc);
void cb_bytecode_free(cb_bytecode *bc);

#endif
