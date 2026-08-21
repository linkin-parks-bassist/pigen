#ifndef PIGEN_EXPRESSION_RESOLVE_H
#define PIGEN_EXPRESSION_RESOLVE_H

#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* Resolves parameter, value, and signal expressions, including packed
 * indices and part-selects.  The resulting semantic expression has a constant
 * identity exactly when its complete tree is a supported constant expression. */
pigen_expr_id pigen_resolve_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression);
/* Uses the same resolver but rejects any tree without a constant identity. */
pigen_expr_id pigen_resolve_constant_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression);

#endif
