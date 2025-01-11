#ifndef cb_disassemble_h
#define cb_disassemble_h

#include <stddef.h>
#include <stdint.h>

#include "code.h"
#include "compiler.h"

int cb_disassemble_recursive(struct cb_code *code);
int cb_disassemble(struct cb_code *code);
int cb_disassemble_one(cb_instruction, size_t offset);

#endif