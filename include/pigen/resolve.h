#ifndef PIGEN_RESOLVE_H
#define PIGEN_RESOLVE_H

#include "pigen/semantic.h"
#include "pigen/semantic_error.h"
#include "pigen/syntax.h"

int pigen_resolve_semantics(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model,
	pigen_semantic_error *error);

#endif
