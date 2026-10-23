#ifndef PIGEN_RESOLVE_H
#define PIGEN_RESOLVE_H

#include "pigen/semantic.h"
#include "pigen/semantic_error.h"
#include "pigen/resolve_policy.h"
#include "pigen/syntax.h"

int pigen_resolve_semantics(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, const pigen_resolve_policy *policy,
	pigen_semantic_error *error);

#endif
