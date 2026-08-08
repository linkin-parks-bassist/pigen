#ifndef PIGEN_ASSIGNMENTS_H
#define PIGEN_ASSIGNMENTS_H

#include "pigen/model.h"

typedef struct { const char *destination; size_t destination_length; const char *expression; size_t expression_length; char destination_kind; } pigen_transfer_item;
typedef struct { const char *prefix_end; pigen_transfer_item *items; size_t count; } pigen_transfer;
int pigen_extract_transport_transfer(const char *start, const char *end, pigen_primitives *primitives, pigen_transfer *transfer);
void pigen_free_transfer(pigen_transfer *transfer);
/* action_kind: 0 = invalidate, 1 = flush, 2 = validate. */
int pigen_extract_clear_action(const char *start, const char *end, pigen_primitives *primitives, const char **prefix_end, const char **target, size_t *target_length, int *action_kind);
void pigen_emit_expression_validity(pigen_string *output, const char *expression, pigen_primitives *primitives);
void pigen_emit_transfer_accept_condition(pigen_string *output, const pigen_transfer *transfer, const char *guard, pigen_primitives *primitives);
void pigen_emit_assignment_routes(pigen_string *output, pigen_assignments *assignments, pigen_primitives *primitives);
void pigen_validate_assignments(pigen_assignments *assignments, pigen_primitives *primitives);
void pigen_validate_clears(pigen_clears *clears, pigen_assignments *assignments, pigen_primitives *primitives);
void pigen_emit_clear_routes(pigen_string *output, pigen_clears *clears, pigen_assignments *assignments, pigen_primitives *primitives);
void pigen_emit_rewritten_expression(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives);
int pigen_emit_conditional_transfer_condition(pigen_string *output, const char *start, const char *end, pigen_primitives *primitives);

#endif
