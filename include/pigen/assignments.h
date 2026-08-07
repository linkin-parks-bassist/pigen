#ifndef PIGEN_ASSIGNMENTS_H
#define PIGEN_ASSIGNMENTS_H

#include "pigen/model.h"

int pigen_extract_transport_assignment(const char *start, const char *end, pigen_primitives *primitives, const char **prefix_end, const char **destination, size_t *destination_length, const char **expression, size_t *expression_length, char *destination_kind);
int pigen_extract_clear_action(const char *start, const char *end, pigen_primitives *primitives, const char **prefix_end, const char **target, size_t *target_length, int *is_flush);
void pigen_emit_expression_validity(pigen_string *output, const char *expression, pigen_primitives *primitives);
void pigen_emit_assignment_routes(pigen_string *output, pigen_assignments *assignments, pigen_primitives *primitives);
void pigen_validate_assignments(pigen_assignments *assignments, pigen_primitives *primitives);
void pigen_validate_clears(pigen_clears *clears, pigen_assignments *assignments, pigen_primitives *primitives);
void pigen_emit_clear_routes(pigen_string *output, pigen_clears *clears, pigen_primitives *primitives);
void pigen_emit_rewritten_expression(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives);
int pigen_emit_conditional_transfer_condition(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives);

#endif
