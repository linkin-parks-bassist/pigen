/* Public expression-resolution orchestration and semantic materialization. */
#include <stdlib.h>

#include "pigen/expression_analysis.h"
#include "pigen/expression_resolve.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef struct {
	pigen_semantic_model *model;
	const pigen_analyzed_expr_arena *arena;
} expression_materializer;

static pigen_expr_id materialize(expression_materializer *materializer,
	pigen_analyzed_expr_id expression);

static pigen_expr_id materialize_conversion(
	expression_materializer *materializer, pigen_expr_id operand,
	pigen_conversion conversion, pigen_source_span span)
{
	if (operand.index == PIGEN_INVALID_ID) return operand;
	if (conversion.kind == PIGEN_CONVERSION_IDENTITY) return operand;
	return pigen_expr_add_conversion(materializer->model, conversion, operand,
		span);
}

static pigen_expr_id materialize_sequence(
	expression_materializer *materializer, const pigen_analyzed_expr *known)
{
	pigen_expr_id *children;
	pigen_expr_id result;
	size_t i;

	if (known->as.sequence.first_child > materializer->arena->child_count ||
		known->as.sequence.child_count > materializer->arena->child_count -
			known->as.sequence.first_child)
		return INVALID_ID(pigen_expr_id);
	children = pigen_resize(NULL,
		known->as.sequence.child_count * sizeof(*children));
	for (i = 0; i < known->as.sequence.child_count; i++)
	{
		children[i] = materialize(materializer,
			materializer->arena->children[
				known->as.sequence.first_child + i]);
		if (children[i].index == PIGEN_INVALID_ID)
		{
			free(children);
			return INVALID_ID(pigen_expr_id);
		}
	}
	result = pigen_expr_add_concatenation(materializer->model, children,
		known->as.sequence.child_count, known->span);
	free(children);
	return result;
}

static pigen_expr_id materialize(expression_materializer *materializer,
	pigen_analyzed_expr_id expression)
{
	const pigen_analyzed_expr *known = pigen_analyzed_expr_get(
		materializer->arena, expression);
	pigen_expr_id operand;
	pigen_expr_id left;
	pigen_expr_id right;
	pigen_expr_id condition;
	pigen_expr_id when_true;
	pigen_expr_id when_false;

	if (!known) return INVALID_ID(pigen_expr_id);
	switch (known->kind)
	{
		case PIGEN_SYNTAX_EXPR_LITERAL:
			if (known->literal_kind == PIGEN_ANALYZED_LITERAL_BITS)
				return pigen_expr_add_bits(materializer->model,
					materializer->arena->literal_states +
						known->as.bits.first_state,
					known->as.bits.state_count, known->data_type, known->span);
			if (known->literal_kind == PIGEN_ANALYZED_LITERAL_EXACT_INTEGER)
				return pigen_expr_add_exact_integer(materializer->model,
					known->as.exact_integer, known->span);
			if (known->literal_kind == PIGEN_ANALYZED_LITERAL_INTEGER)
				return pigen_expr_add_integer(materializer->model,
					known->as.integer, known->data_type, known->span);
			return INVALID_ID(pigen_expr_id);
		case PIGEN_SYNTAX_EXPR_NAME:
			return pigen_expr_add_symbol(materializer->model, known->as.symbol,
				known->data_type, known->span);
		case PIGEN_SYNTAX_EXPR_GROUP:
			operand = materialize(materializer, known->as.group.operand);
			return pigen_expr_add_group(materializer->model, operand, known->span);
		case PIGEN_SYNTAX_EXPR_UNARY:
			operand = materialize(materializer, known->as.unary.operand);
			operand = materialize_conversion(materializer, operand,
				known->as.unary.resolution.operand_conversion, known->span);
			return pigen_expr_add_unary(materializer->model,
				known->as.unary.resolution.operation, operand, known->span);
		case PIGEN_SYNTAX_EXPR_BINARY:
			left = materialize(materializer, known->as.binary.left);
			right = materialize(materializer, known->as.binary.right);
			left = materialize_conversion(materializer, left,
				known->as.binary.resolution.left_conversion, known->span);
			right = materialize_conversion(materializer, right,
				known->as.binary.resolution.right_conversion, known->span);
			return pigen_expr_add_binary(materializer->model,
				known->as.binary.resolution.operation, left, right, known->span);
		case PIGEN_SYNTAX_EXPR_CONDITIONAL:
			condition = materialize(materializer,
				known->as.conditional.condition);
			when_true = materialize(materializer,
				known->as.conditional.when_true);
			when_false = materialize(materializer,
				known->as.conditional.when_false);
			condition = materialize_conversion(materializer, condition,
				known->as.conditional.resolution.condition_conversion, known->span);
			when_true = materialize_conversion(materializer, when_true,
				known->as.conditional.resolution.when_true_conversion, known->span);
			when_false = materialize_conversion(materializer, when_false,
				known->as.conditional.resolution.when_false_conversion, known->span);
			return pigen_expr_add_conditional(materializer->model,
				known->as.conditional.resolution.operation, condition, when_true,
				when_false, known->span);
		case PIGEN_SYNTAX_EXPR_CAST:
			operand = materialize(materializer, known->as.conversion.operand);
			return materialize_conversion(materializer, operand,
				known->as.conversion.conversion, known->span);
		case PIGEN_SYNTAX_EXPR_INDEX:
			left = materialize(materializer, known->as.index.base);
			right = materialize(materializer, known->as.index.index);
			return pigen_expr_add_index(materializer->model, left, right,
				known->span);
		case PIGEN_SYNTAX_EXPR_SELECT:
			operand = materialize(materializer, known->as.select.base);
			left = materialize(materializer, known->as.select.left);
			right = materialize(materializer, known->as.select.right);
			return pigen_expr_add_select(materializer->model, operand, left,
				right, known->as.select.kind, known->span);
		case PIGEN_SYNTAX_EXPR_CONCATENATION:
			return materialize_sequence(materializer, known);
		default:
			return INVALID_ID(pigen_expr_id);
	}
}

static pigen_expr_id resolve_with_policy(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression, int constant_only,
	pigen_literal_domain literal_domain,
	const pigen_resolve_policy *policy, pigen_semantic_error *error)
{
	pigen_analyzed_expr_arena arena = {0};
	pigen_analyzed_expr_id analyzed;
	expression_materializer materializer;
	pigen_expr_id result;
	size_t expression_count;
	size_t child_count;
	size_t constraint_count;
	size_t i;

	if (!policy || !policy->maximum_generated_bits ||
		!pigen_analyze_expression(syntax, model, scope, expression,
			constant_only, literal_domain, policy, &arena,
			&analyzed, error))
	{
		pigen_free_analyzed_expr_arena(&arena);
		return INVALID_ID(pigen_expr_id);
	}
	expression_count = model->expression_count;
	child_count = model->expression_child_count;
	constraint_count = model->width_constraint_count;
	materializer.model = model;
	materializer.arena = &arena;
	result = materialize(&materializer, analyzed);
	for (i = 0; result.index != PIGEN_INVALID_ID &&
		i < arena.constraint_count; i++)
		if (!pigen_width_constraint_add(model, arena.constraints[i].width,
			arena.constraints[i].maximum_bits, arena.constraints[i].span))
			result = INVALID_ID(pigen_expr_id);
	if (result.index == PIGEN_INVALID_ID)
	{
		model->expression_count = expression_count;
		model->expression_child_count = child_count;
		model->width_constraint_count = constraint_count;
		if (error && !error->message)
		{
			error->span = arena.nodes[analyzed.index].span;
			error->message = "internal expression materialization failure";
		}
	}
	pigen_free_analyzed_expr_arena(&arena);
	return result;
}

pigen_expr_id pigen_resolve_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,
	pigen_semantic_error *error)
{
	return resolve_with_policy(syntax, model, scope, expression, 0,
		PIGEN_LITERAL_DOMAIN_PIGEN, policy, error);
}

pigen_expr_id pigen_resolve_constant_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	pigen_literal_domain literal_domain,
	const pigen_resolve_policy *policy,
	pigen_semantic_error *error)
{
	return resolve_with_policy(syntax, model, scope, expression, 1,
		literal_domain, policy, error);
}

pigen_expr_id pigen_resolve_assignment_value(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	pigen_data_type_id target, const pigen_resolve_policy *policy,
	pigen_semantic_error *error)
{
	size_t expression_count = model ? model->expression_count : 0;
	size_t child_count = model ? model->expression_child_count : 0;
	size_t constraint_count = model ? model->width_constraint_count : 0;
	pigen_expr_id value = pigen_resolve_expression(syntax, model, scope,
		expression, policy, error);
	const pigen_semantic_expr *known = pigen_expr_get(model, value);
	pigen_conversion conversion;
	pigen_expr_id result;

	if (!known || !pigen_data_type_resolve_assignment_conversion(model,
		known->data_type, target, &conversion))
	{
		if (model)
		{
			model->expression_count = expression_count;
			model->expression_child_count = child_count;
			model->width_constraint_count = constraint_count;
		}
		if (error && !error->message)
		{
			error->span = known ? known->span :
				(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0};
			error->message = "invalid assignment conversion";
		}
		return INVALID_ID(pigen_expr_id);
	}
	result = materialize_conversion(&(expression_materializer){model, NULL},
		value, conversion, known->span);
	if (result.index == PIGEN_INVALID_ID)
	{
		model->expression_count = expression_count;
		model->expression_child_count = child_count;
		model->width_constraint_count = constraint_count;
	}
	return result;
}
