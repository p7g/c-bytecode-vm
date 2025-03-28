#ifndef cb_compiler_h
#define cb_compiler_h

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "module.h"

typedef size_t cb_instruction;

int cb_compile_file(cb_modspec *module, FILE *f);
int cb_compile_string(cb_modspec *module, const char *source, size_t source_len);
struct cb_code *cb_repl_compile(cb_modspec *modspec, const char *source,
		size_t source_len);

#endif