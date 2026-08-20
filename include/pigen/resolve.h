#ifndef PIGEN_RESOLVE_H
#define PIGEN_RESOLVE_H

#include "pigen/semantic.h"
#include "pigen/syntax.h"

typedef struct {
	pigen_origin_id origin;
	pigen_source_span span;
	const char *message;
} pigen_resolve_error;

int pigen_resolve_semantics(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,
	pigen_resolve_error *error);

#endif
