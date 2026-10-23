#ifndef PIGEN_DECLARATIONS_H
#define PIGEN_DECLARATIONS_H

#include "pigen/model.h"

char pigen_declaration_transfer_type(const char *start, const char *end, const char **keyword, const char **after_keyword);
int pigen_transfer_type_has_storage(char transfer_type);
void pigen_append_control_name(pigen_string *output, const char *name, size_t name_length, const char *suffix);
void pigen_emit_signal_condition(pigen_string *output, pigen_primitive *primitive, const char *suffix);
void pigen_emit_internal_declaration(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives);
void pigen_emit_ports(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives);
void pigen_emit_port_adapters(pigen_string *output, pigen_primitives *primitives);
void pigen_set_reset_connection(int has_reset);
const char *pigen_reset_connection(void);

#endif
