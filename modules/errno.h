#ifndef cb_modules_errno_h
#define cb_modules_errno_h

#include "module.h"

void cb_errno_build_spec(cb_modspec *spec);
void cb_errno_instantiate(struct cb_module *mod);

#endif
