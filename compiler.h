#ifndef cb_compiler_h
#define cb_compiler_h

#include <stddef.h>
#include <stdint.h>

int cb_compile(const char *input, uint8_t **bc_out, size_t *bc_len_out);
int cb_compile_file(const char *name, uint8_t **bc_out, size_t *bc_len_out);

#endif
