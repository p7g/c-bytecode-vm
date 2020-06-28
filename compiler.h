#ifndef cb_compiler_h
#define cb_compiler_h

#include <stddef.h>
#include <stdint.h>

typedef struct bytecode cb_bytecode;

int cb_compile(const char *input, cb_bytecode **bc_out);
int cb_compile_file(const char *name, cb_bytecode **bc_out);

uint8_t cb_bytecode_get(cb_bytecode *bc, size_t idx);
size_t cb_bytecode_len(cb_bytecode *bc);
void cb_bytecode_free(cb_bytecode *bc);

#endif
