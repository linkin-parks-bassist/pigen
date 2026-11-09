/* Public expression-resolution orchestration and semantic materialization. */
#include <stdlib.h>
#include <string.h>

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

static int call_is_intrinsic(const pigen_syntax_tree *syntax,
	pigen_syntax_expr_id expression)
{
	const pigen_syntax_expr *call = pigen_syntax_expr_get(
		&syntax->expressions, expression);
	const pigen_syntax_expr *callee;
	const pigen_expanded_token *token;
	const char *name;
	size_t length = 0;

	if (!call || call->kind != PIGEN_SYNTAX_EXPR_CALL) return 0;
	callee = pigen_syntax_expr_get(&syntax->expressions, call->as.call.callee);
	if (!callee || callee->kind != PIGEN_SYNTAX_EXPR_NAME) return 0;
	token = pigen_expanded_token_get(syntax->expanded, callee->as.atom.token);
	name = token ? pigen_expanded_token_text(syntax->expanded, token, &length) :
		NULL;
	return name && ((length == 7 && !memcmp(name, "accepts", 7)) ||
		(length == 5 && !memcmp(name, "valid", 5)) ||
		(length == 5 && !memcmp(name, "ready", 5)) ||
		(length == 4 && !memcmp(name, "peek", 4)));
}

static int contains_intrinsic(const pigen_syntax_tree *syntax,
	pigen_syntax_expr_id expression)
{
	const pigen_syntax_expr *known = pigen_syntax_expr_get(
		&syntax->expressions, expression);

	if (!known) return 0;
	if (known->kind == PIGEN_SYNTAX_EXPR_CALL)
		return call_is_intrinsic(syntax, expression);
	if (known->kind == PIGEN_SYNTAX_EXPR_GROUP)
		return contains_intrinsic(syntax, known->as.group.operand);
	if (known->kind == PIGEN_SYNTAX_EXPR_UNARY)
		return contains_intrinsic(syntax, known->as.unary.operand);
	if (known->kind == PIGEN_SYNTAX_EXPR_BINARY)
		return contains_intrinsic(syntax, known->as.binary.left) ||
			contains_intrinsic(syntax, known->as.binary.right);
	if (known->kind == PIGEN_SYNTAX_EXPR_CONDITIONAL)
		return contains_intrinsic(syntax, known->as.conditional.condition) ||
			contains_intrinsic(syntax, known->as.conditional.when_true) ||
			contains_intrinsic(syntax, known->as.conditional.when_false);
	return 0;
}

static int resolve_intrinsic(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_expr_id expression, const pigen_resolve_policy *policy,
	pigen_semantic_error *error, pigen_expr_id *result)
{
	const pigen_syntax_expr *call = pigen_syntax_expr_get(
		&syntax->expressions, expression);
	const pigen_syntax_expr *callee;
	const pigen_syntax_expr_id *arguments;
	const pigen_expanded_token *token;
	const char *name;
	size_t name_length = 0;

	if (!call) return 0;
	if (call->kind != PIGEN_SYNTAX_EXPR_CALL)
	{
		if (!contains_intrinsic(syntax, expression))
			return 0;
		if (call->kind == PIGEN_SYNTAX_EXPR_GROUP)
		{
			pigen_expr_id operand = pigen_resolve_expression(syntax, model,
				scope, call->as.group.operand, policy, error);
			*result = operand.index == PIGEN_INVALID_ID ? operand :
				pigen_expr_add_group(model, operand,
					call->location.source_span);
			return 1;
		}
		if (call->kind == PIGEN_SYNTAX_EXPR_BINARY)
		{
			pigen_expr_id left = pigen_resolve_expression(syntax, model,
				scope, call->as.binary.left, policy, error);
			pigen_expr_id right = pigen_resolve_expression(syntax, model,
				scope, call->as.binary.right, policy, error);
			pigen_binary_operator operation;
			pigen_binary_resolution resolution;
			const pigen_semantic_expr *l = pigen_expr_get(model, left);
			const pigen_semantic_expr *r = pigen_expr_get(model, right);

			if (!l || !r) { *result = INVALID_ID(pigen_expr_id); return 1; }
			{
				static const pigen_binary_operator map[] = {
					[PIGEN_SYNTAX_OP_LOGICAL_AND] = PIGEN_BINARY_LOGICAL_AND,
					[PIGEN_SYNTAX_OP_LOGICAL_OR] = PIGEN_BINARY_LOGICAL_OR,
					[PIGEN_SYNTAX_OP_BITWISE_AND] = PIGEN_BINARY_BITWISE_AND,
					[PIGEN_SYNTAX_OP_BITWISE_OR] = PIGEN_BINARY_BITWISE_OR,
					[PIGEN_SYNTAX_OP_BITWISE_XOR] = PIGEN_BINARY_BITWISE_XOR,
					[PIGEN_SYNTAX_OP_EQUAL] = PIGEN_BINARY_EQUAL,
					[PIGEN_SYNTAX_OP_NOT_EQUAL] = PIGEN_BINARY_NOT_EQUAL,
					[PIGEN_SYNTAX_OP_LESS] = PIGEN_BINARY_LESS,
					[PIGEN_SYNTAX_OP_LESS_EQUAL] = PIGEN_BINARY_LESS_EQUAL,
					[PIGEN_SYNTAX_OP_GREATER] = PIGEN_BINARY_GREATER,
					[PIGEN_SYNTAX_OP_GREATER_EQUAL] = PIGEN_BINARY_GREATER_EQUAL,
					[PIGEN_SYNTAX_OP_ADD] = PIGEN_BINARY_ADD,
					[PIGEN_SYNTAX_OP_SUBTRACT] = PIGEN_BINARY_SUBTRACT
				};

				if ((size_t)call->as.binary.operator >=
					sizeof(map)/sizeof(map[0]) ||
					map[call->as.binary.operator] == 0)
					{ *result = INVALID_ID(pigen_expr_id); return 1; }
				operation = map[call->as.binary.operator];
			}
			if (!pigen_data_type_resolve_binary_operation(model, operation,
				l->data_type, r->data_type, &resolution))
				*result = INVALID_ID(pigen_expr_id);
			else
				*result = pigen_expr_add_binary(model, resolution.operation,
					left, right, call->location.source_span);
			return 1;
		}
		return 0;
	}
	callee = pigen_syntax_expr_get(&syntax->expressions, call->as.call.callee);
	if (!callee || callee->kind != PIGEN_SYNTAX_EXPR_NAME)
		return 0;
	token = pigen_expanded_token_get(syntax->expanded,
		callee->as.atom.token);
	name = token ? pigen_expanded_token_text(syntax->expanded, token,
		&name_length) : NULL;
	if (!name) return 0;
	arguments = pigen_syntax_expr_children(&syntax->expressions,
		call->as.call.first_argument, call->as.call.argument_count);
	if (!arguments) return 0;

	if (name_length == 7 && !memcmp(name, "accepts", 7) &&
		call->as.call.argument_count == 2)
	{
		pigen_expr_id destination_expression = pigen_resolve_expression(
			syntax, model, scope, arguments[0], policy, error);
		pigen_expr_id source = pigen_resolve_expression(syntax, model,
			scope, arguments[1], policy, error);
		pigen_lvalue_id destination =
			pigen_lvalue_resolve(model, destination_expression);
		const pigen_semantic_expr *source_expression =
			pigen_expr_get(model, source);
		pigen_signal_id source_signal = source_expression ?
			pigen_symbol_signal(model, source_expression->as.symbol) :
			(pigen_signal_id){PIGEN_INVALID_ID};

		if (source_expression && source_expression->kind ==
			PIGEN_EXPR_SYMBOL)
			source_signal = pigen_symbol_signal(model,
				source_expression->as.symbol);
		if (destination.index == PIGEN_INVALID_ID ||
			source_signal.index == PIGEN_INVALID_ID)
			*result = INVALID_ID(pigen_expr_id);
		else
			*result = pigen_expr_add_accepts(model, destination, source,
				source_signal, call->location.source_span);
		return 1;
	}
	if ((name_length == 5 && !memcmp(name, "valid", 5)) ||
		(name_length == 5 && !memcmp(name, "ready", 5)))
	{
		pigen_expr_id argument;
		const pigen_semantic_expr *known;
		pigen_signal_id signal;
		pigen_semantic_expr_kind kind =
			name[0] == 'v' ? PIGEN_EXPR_VALID : PIGEN_EXPR_READY;

		if (call->as.call.argument_count != 1)
			return 0;
		argument = pigen_resolve_expression(syntax, model, scope,
			arguments[0], policy, error);
		known = pigen_expr_get(model, argument);
		signal = known && known->kind == PIGEN_EXPR_SYMBOL ?
			pigen_symbol_signal(model, known->as.symbol) :
			(pigen_signal_id){PIGEN_INVALID_ID};
		*result = signal.index == PIGEN_INVALID_ID ?
			INVALID_ID(pigen_expr_id) :
			pigen_expr_add_control(model, kind, signal,
				call->location.source_span);
		return 1;
	}
	if (name_length == 4 && !memcmp(name, "peek", 4) &&
		call->as.call.argument_count == 1)
	{
		pigen_expr_id source = pigen_resolve_expression(syntax, model,
			scope, arguments[0], policy, error);

		*result = pigen_expr_add_peek(model, source,
			call->location.source_span);
		return 1;
	}
	return 0;
}

pigen_expr_id pigen_resolve_expression(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	const pigen_resolve_policy *policy,
	pigen_semantic_error *error)
{
	pigen_expr_id intrinsic = INVALID_ID(pigen_expr_id);

	if (resolve_intrinsic(syntax, model, scope, expression, policy, error,
		&intrinsic))
		return intrinsic;
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
