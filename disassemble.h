#ifndef cb_disassemble_h
#define cb_disassemble_h

#include <stddef.h>
#include <stdint.h>

#include "compiler.h"

int cb_disassemble(cb_bytecode *bytecode);
int cb_disassemble_one(cb_bytecode *bytecode, size_t pc);
int cb_disassemble_range(cb_bytecode *bytecode, size_t start, size_t end);

#endif
