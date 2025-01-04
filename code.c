#include <stdlib.h>

#include "code.h"
#include "constant.h"
#include "module.h"

struct cb_code *cb_code_new(void)
{
	return calloc(1, sizeof(struct cb_code));
}

void cb_code_free(struct cb_code *code)
{
	free(code->bytecode);
	for (int i = 0; i < code->nconsts; i += 1)
		cb_const_free(&code->const_pool[i]);
}
