#ifndef PIGEN_EXPRESSION_RESOLVE_H
#define PIGEN_EXPRESSION_RESOLVE_H

#include "pigen/expression_analysis.h"
#include "pigen/semantic.h"
#include "pigen/semantic_error.h"
#include "pigen/resolve_policy.h"
#include "pigen/syntax.h"

/* Resolves parameter, value, and signal expressions, including packed
 * indices and part-selects. Pure constant trees retain constant recipes;
 * evaluated-value records deliberately preserve their evaluations unfolded. */
pigen_expr_id pigen_resolve_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,
	pigen_semantic_error *error);
/* Native control expressions receive their self-determined SV context;
 * concrete Pigen numerical subtrees retain family arithmetic. */
pigen_expr_id pigen_resolve_control_expression(
	const pigen_syntax_tree *syntax,pigen_semantic_model *model,
	pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_semantic_error *error);
/* Procedural case operands retain the SystemVerilog unsized-literal domain,
 * including inside trees containing handshake observations. */
pigen_expr_id pigen_resolve_case_expression(
	const pigen_syntax_tree *syntax,pigen_semantic_model *model,
	pigen_scope_id scope,pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,pigen_semantic_error *error);
/* Apply the common case comparison context to an intrinsically typed value. */
pigen_expr_id pigen_resolve_case_context(pigen_semantic_model *model,
	pigen_expr_id value,size_t width,pigen_signedness sign);
/* Uses the same resolver but rejects any tree without a constant identity. */
pigen_expr_id pigen_resolve_constant_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	pigen_literal_domain literal_domain,
	const pigen_resolve_policy *policy,
	pigen_semantic_error *error);
/* Supplies destination context to record patterns and native SV expressions.
 * Explicit cast/self-determined boundaries and Pigen arithmetic stay typed. */
pigen_expr_id pigen_resolve_assignment_value(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	pigen_data_type_id target, const pigen_resolve_policy *policy,
	pigen_semantic_error *error);

/* Adds unpacked shape context, ordinal bounds mapping and element conversion. */
pigen_expr_id pigen_resolve_shaped_assignment_value(
 const pigen_syntax_tree *syntax,pigen_semantic_model *model,pigen_scope_id scope,
 pigen_syntax_expr_id expression,pigen_data_type_id target,pigen_shape_id shape,
 const pigen_resolve_policy *policy,pigen_semantic_error *error);

#endif
