#ifndef PIGEN_TYPE_RESOLVE_H
#define PIGEN_TYPE_RESOLVE_H

#include "pigen/semantic.h"
#include "pigen/semantic_error.h"
#include "pigen/resolve_policy.h"
#include "pigen/syntax.h"

pigen_symbol_id pigen_resolve_type_symbol(const pigen_syntax_tree *syntax,
 pigen_semantic_model *model,pigen_scope_id scope,pigen_syntax_type_id type);
pigen_data_type_id pigen_resolve_type(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_type_id syntax_type, pigen_semantic_error *error);

pigen_data_type_id pigen_resolve_cast_type(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	const pigen_syntax_expr *cast, pigen_data_type_id operand,
	const pigen_resolve_policy *policy, pigen_semantic_error *error);

#endif
