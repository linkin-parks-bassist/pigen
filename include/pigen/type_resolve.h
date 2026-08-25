#ifndef PIGEN_TYPE_RESOLVE_H
#define PIGEN_TYPE_RESOLVE_H

#include "pigen/semantic.h"
#include "pigen/semantic_error.h"
#include "pigen/syntax.h"

pigen_data_type_id pigen_resolve_type(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_type_id syntax_type, pigen_semantic_error *error);

#endif
