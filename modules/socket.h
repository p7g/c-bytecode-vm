#ifndef cb_modules_socket_h
#define cb_modules_socket_h

#include "module.h"

void cb_socket_build_spec(cb_modspec *spec);
void cb_socket_instantiate(struct cb_module *mod);

#endif