#include "opcode.h"

const char *cb_opcode_name(enum cb_opcode op)
{
#define CASE(O) case O: return #O;
	switch (op) {
	CB_OPCODE_LIST(CASE)
	default:
		return "";
	}
#undef CASE
}
