#ifndef PIGEN_TYPE_SYNTAX_H
#define PIGEN_TYPE_SYNTAX_H

#include <stddef.h>

#include "pigen/syntax_common.h"

typedef struct pigen_syntax_expr_arena pigen_syntax_expr_arena;

typedef enum {
	PIGEN_SYNTAX_TYPE_COUNT,
	PIGEN_SYNTAX_TYPE_RANGE
} pigen_syntax_type_argument_kind;

typedef struct {
	pigen_syntax_type_argument_kind kind;
	pigen_syntax_location location;
	union {
		pigen_syntax_expr_id count;
		struct {
			pigen_syntax_expr_id left;
			pigen_syntax_expr_id right;
		} range;
	} as;
} pigen_syntax_type_argument;

typedef struct {
	pigen_token_id base;
	pigen_syntax_signedness signedness;
	pigen_syntax_location location;
	size_t first_argument;
	size_t argument_count;
} pigen_syntax_type;

typedef struct {
	pigen_syntax_type *nodes;
	size_t node_count;
	size_t node_capacity;
	pigen_syntax_type_argument *arguments;
	size_t argument_count;
	size_t argument_capacity;
} pigen_syntax_type_arena;

int pigen_parse_type_prefix(const pigen_expanded_source *source,
	size_t first, size_t limit, pigen_syntax_expr_arena *expressions,
	pigen_syntax_type_arena *types, pigen_syntax_type_id *type,
	size_t *after, pigen_syntax_error *error);
const pigen_syntax_type *pigen_syntax_type_get(
	const pigen_syntax_type_arena *arena, pigen_syntax_type_id type);
const pigen_syntax_type_argument *pigen_syntax_type_arguments(
	const pigen_syntax_type_arena *arena, size_t first, size_t count);
void pigen_free_syntax_type_arena(pigen_syntax_type_arena *arena);

#endif
