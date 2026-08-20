/* Stable semantic identities, lexical scopes, symbols, and structured types. */
#include <stdlib.h>
#include <string.h>

#include "pigen/semantic.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static int id_capacity_available(size_t count)
{
	return count < PIGEN_INVALID_ID;
}

static int span_contains(pigen_source_span outer, pigen_source_span inner)
{
	return outer.source.index == inner.source.index &&
		outer.start <= inner.start && inner.end <= outer.end;
}

static int spans_equal(pigen_source_span left, pigen_source_span right)
{
	return left.source.index == right.source.index && left.start == right.start &&
		left.end == right.end;
}

static int same_name(const pigen_semantic_model *model, pigen_source_span left,
	pigen_source_span right)
{
	const char *left_text;
	const char *right_text;
	size_t left_length;
	size_t right_length;

	left_text = pigen_source_span_text(model->sources, left, &left_length);
	right_text = pigen_source_span_text(model->sources, right, &right_length);
	return left_text && right_text && left_length == right_length &&
		!memcmp(left_text, right_text, left_length);
}

static int dimensions_equal(const pigen_semantic_model *model,
	const pigen_semantic_type *type, const pigen_packed_dimension *dimensions,
	size_t count)
{
	size_t i;

	if (type->dimension_count != count)
		return 0;
	for (i = 0; i < count; i++)
	{
		const pigen_packed_dimension *known =
			&model->dimensions[type->first_dimension + i];
		if (known->left.index != dimensions[i].left.index ||
			known->right.index != dimensions[i].right.index)
			return 0;
	}
	return 1;
}

void pigen_semantic_init(pigen_semantic_model *model,
	const pigen_source_manager *sources)
{
	*model = (pigen_semantic_model){0};
	model->sources = sources;
	model->compilation_scope = INVALID_ID(pigen_scope_id);
	model->integer_type = INVALID_ID(pigen_type_id);
	model->boolean_result_type = INVALID_ID(pigen_type_id);
	model->true_predicate = INVALID_ID(pigen_predicate_id);
	model->false_predicate = INVALID_ID(pigen_predicate_id);
}

pigen_type_id pigen_type_intern(pigen_semantic_model *model,
	pigen_semantic_type_kind kind, pigen_signedness signedness,
	pigen_symbol_id named_symbol, const pigen_packed_dimension *dimensions,
	size_t dimension_count)
{
	size_t i;
	pigen_semantic_type *type;
	pigen_type_id result;

	if (!model->sources ||
		kind < PIGEN_TYPE_LOGIC || kind > PIGEN_TYPE_NAMED ||
		signedness < PIGEN_SIGN_IMPLICIT || signedness > PIGEN_SIGN_SIGNED ||
		(dimension_count && !dimensions) || !id_capacity_available(model->type_count))
		return INVALID_ID(pigen_type_id);
	if (kind == PIGEN_TYPE_NAMED)
	{
		const pigen_symbol *symbol = pigen_symbol_get(model, named_symbol);
		if (!symbol || symbol->kind != PIGEN_SYMBOL_TYPEDEF)
			return INVALID_ID(pigen_type_id);
	}
	else if (named_symbol.index != PIGEN_INVALID_ID)
		return INVALID_ID(pigen_type_id);
	for (i = 0; i < dimension_count; i++)
		if (!pigen_const_expr_get(model, dimensions[i].left) ||
			!pigen_const_expr_get(model, dimensions[i].right))
			return INVALID_ID(pigen_type_id);

	for (i = 0; i < model->type_count; i++)
	{
		type = &model->types[i];
		if (type->kind == kind && type->signedness == signedness &&
			type->named_symbol.index == named_symbol.index &&
			dimensions_equal(model, type, dimensions, dimension_count))
			return (pigen_type_id){(uint32_t)i};
	}

	if (model->type_count == model->type_capacity)
	{
		model->type_capacity = model->type_capacity ? model->type_capacity * 2 : 16;
		model->types = pigen_resize(model->types,
			model->type_capacity * sizeof(*model->types));
	}
	if (model->dimension_count + dimension_count > model->dimension_capacity)
	{
		size_t capacity = model->dimension_capacity ? model->dimension_capacity * 2 : 16;
		while (capacity < model->dimension_count + dimension_count)
			capacity *= 2;
		model->dimensions = pigen_resize(model->dimensions,
			capacity * sizeof(*model->dimensions));
		model->dimension_capacity = capacity;
	}

	result = (pigen_type_id){(uint32_t)model->type_count};
	type = &model->types[model->type_count++];
	*type = (pigen_semantic_type){kind, signedness, named_symbol,
		model->dimension_count, dimension_count};
	if (dimension_count)
		memcpy(model->dimensions + model->dimension_count, dimensions,
			dimension_count * sizeof(*dimensions));
	model->dimension_count += dimension_count;
	return result;
}

pigen_type_id pigen_semantic_integer_type(pigen_semantic_model *model)
{
	if (model->integer_type.index == PIGEN_INVALID_ID)
		model->integer_type = pigen_type_intern(model, PIGEN_TYPE_INTEGER,
			PIGEN_SIGN_SIGNED, INVALID_ID(pigen_symbol_id), NULL, 0);
	return model->integer_type;
}

pigen_type_id pigen_semantic_boolean_result_type(pigen_semantic_model *model)
{
	if (model->boolean_result_type.index == PIGEN_INVALID_ID)
		model->boolean_result_type = pigen_type_intern(model, PIGEN_TYPE_LOGIC,
			PIGEN_SIGN_UNSIGNED, INVALID_ID(pigen_symbol_id), NULL, 0);
	return model->boolean_result_type;
}

const pigen_semantic_type *pigen_type_get(const pigen_semantic_model *model,
	pigen_type_id type)
{
	if (type.index == PIGEN_INVALID_ID || type.index >= model->type_count)
		return NULL;
	return &model->types[type.index];
}

const pigen_packed_dimension *pigen_type_dimensions(
	const pigen_semantic_model *model, pigen_type_id type)
{
	const pigen_semantic_type *known = pigen_type_get(model, type);
	if (!known || !known->dimension_count)
		return NULL;
	return model->dimensions + known->first_dimension;
}

static int const_expressions_equal(const pigen_semantic_model *model,
	const pigen_const_expr *left, const pigen_const_expr *right)
{
	if (left->kind != right->kind || left->type.index != right->type.index)
		return 0;
	switch (left->kind)
	{
		case PIGEN_CONST_EXPR_INTEGER:
			return left->as.integer == right->as.integer;
		case PIGEN_CONST_EXPR_BITS:
			return left->as.bits.state_count == right->as.bits.state_count &&
				!memcmp(model->literal_states + left->as.bits.first_state,
					model->literal_states + right->as.bits.first_state,
					left->as.bits.state_count *
						sizeof(*model->literal_states));
		case PIGEN_CONST_EXPR_SYMBOL:
			return left->as.symbol.index == right->as.symbol.index;
		case PIGEN_CONST_EXPR_UNARY:
			return left->as.unary.operator == right->as.unary.operator &&
				left->as.unary.operand.index == right->as.unary.operand.index;
		case PIGEN_CONST_EXPR_BINARY:
			return left->as.binary.operator == right->as.binary.operator &&
				left->as.binary.left.index == right->as.binary.left.index &&
				left->as.binary.right.index == right->as.binary.right.index;
		case PIGEN_CONST_EXPR_CONDITIONAL:
			return left->as.conditional.condition.index ==
					right->as.conditional.condition.index &&
				left->as.conditional.when_true.index ==
					right->as.conditional.when_true.index &&
				left->as.conditional.when_false.index ==
					right->as.conditional.when_false.index;
	}
	return 0;
}

static pigen_const_expr_id intern_const_expression(
	pigen_semantic_model *model, pigen_const_expr expression)
{
	size_t i;
	pigen_const_expr_id result;

	if (!pigen_type_get(model, expression.type) ||
		!id_capacity_available(model->constant_expression_count))
		return INVALID_ID(pigen_const_expr_id);
	for (i = 0; i < model->constant_expression_count; i++)
		if (const_expressions_equal(model, &model->constant_expressions[i],
			&expression))
			return (pigen_const_expr_id){(uint32_t)i};
	if (model->constant_expression_count ==
		model->constant_expression_capacity)
	{
		model->constant_expression_capacity =
			model->constant_expression_capacity ?
			model->constant_expression_capacity * 2 : 16;
		model->constant_expressions = pigen_resize(model->constant_expressions,
			model->constant_expression_capacity *
			sizeof(*model->constant_expressions));
	}
	result = (pigen_const_expr_id){
		(uint32_t)model->constant_expression_count};
	model->constant_expressions[model->constant_expression_count++] = expression;
	return result;
}

pigen_const_expr_id pigen_const_expr_intern_integer(
	pigen_semantic_model *model, uint64_t value, pigen_type_id type)
{
	pigen_const_expr expression = {0};
	expression.kind = PIGEN_CONST_EXPR_INTEGER;
	expression.type = type;
	expression.as.integer = value;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_bits(pigen_semantic_model *model,
	const pigen_bit_state *states, size_t state_count, pigen_type_id type)
{
	pigen_const_expr expression = {0};
	pigen_const_expr_id result;
	size_t i;

	if (!states || !state_count ||
		model->literal_state_count > SIZE_MAX - state_count)
		return INVALID_ID(pigen_const_expr_id);
	for (i = 0; i < state_count; i++)
		if (states[i] > PIGEN_BIT_Z)
			return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_BITS;
	expression.type = type;
	expression.as.bits.first_state = model->literal_state_count;
	expression.as.bits.state_count = state_count;
	for (i = 0; i < model->constant_expression_count; i++)
		if (model->constant_expressions[i].kind == PIGEN_CONST_EXPR_BITS &&
			model->constant_expressions[i].type.index == type.index &&
			model->constant_expressions[i].as.bits.state_count == state_count &&
			!memcmp(model->literal_states +
					model->constant_expressions[i].as.bits.first_state,
				states, state_count * sizeof(*states)))
			return (pigen_const_expr_id){(uint32_t)i};
	if (model->literal_state_count + state_count >
		model->literal_state_capacity)
	{
		size_t capacity = model->literal_state_capacity ?
			model->literal_state_capacity * 2 : 32;
		while (capacity < model->literal_state_count + state_count)
			capacity *= 2;
		model->literal_states = pigen_resize(model->literal_states,
			capacity * sizeof(*model->literal_states));
		model->literal_state_capacity = capacity;
	}
	memcpy(model->literal_states + model->literal_state_count, states,
		state_count * sizeof(*states));
	model->literal_state_count += state_count;
	result = intern_const_expression(model, expression);
	if (result.index == PIGEN_INVALID_ID)
		model->literal_state_count -= state_count;
	return result;
}

pigen_const_expr_id pigen_const_expr_intern_symbol(
	pigen_semantic_model *model, pigen_symbol_id symbol, pigen_type_id type)
{
	const pigen_symbol *known = pigen_symbol_get(model, symbol);
	pigen_const_expr expression = {0};

	if (!known || known->kind != PIGEN_SYMBOL_PARAMETER ||
		known->type.index != type.index)
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_SYMBOL;
	expression.type = type;
	expression.as.symbol = symbol;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_unary(
	pigen_semantic_model *model, pigen_unary_operator operator,
	pigen_const_expr_id operand, pigen_type_id type)
{
	pigen_const_expr expression = {0};

	if (operator < PIGEN_UNARY_POSITIVE ||
		operator > PIGEN_UNARY_REDUCTION_XNOR ||
		!pigen_const_expr_get(model, operand))
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_UNARY;
	expression.type = type;
	expression.as.unary.operator = operator;
	expression.as.unary.operand = operand;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_binary(
	pigen_semantic_model *model, pigen_binary_operator operator,
	pigen_const_expr_id left, pigen_const_expr_id right, pigen_type_id type)
{
	pigen_const_expr expression = {0};

	if (operator < PIGEN_BINARY_ADD ||
		operator > PIGEN_BINARY_LOGICAL_OR ||
		!pigen_const_expr_get(model, left) ||
		!pigen_const_expr_get(model, right))
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_BINARY;
	expression.type = type;
	expression.as.binary.operator = operator;
	expression.as.binary.left = left;
	expression.as.binary.right = right;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_conditional(
	pigen_semantic_model *model, pigen_const_expr_id condition,
	pigen_const_expr_id when_true, pigen_const_expr_id when_false,
	pigen_type_id type)
{
	const pigen_const_expr *condition_expression =
		pigen_const_expr_get(model, condition);
	const pigen_const_expr *true_expression =
		pigen_const_expr_get(model, when_true);
	const pigen_const_expr *false_expression =
		pigen_const_expr_get(model, when_false);
	pigen_const_expr expression = {0};

	if (!condition_expression || !true_expression || !false_expression ||
		true_expression->type.index != type.index ||
		false_expression->type.index != type.index)
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_CONDITIONAL;
	expression.type = type;
	expression.as.conditional.condition = condition;
	expression.as.conditional.when_true = when_true;
	expression.as.conditional.when_false = when_false;
	return intern_const_expression(model, expression);
}

const pigen_const_expr *pigen_const_expr_get(
	const pigen_semantic_model *model, pigen_const_expr_id expression)
{
	if (expression.index == PIGEN_INVALID_ID ||
		expression.index >= model->constant_expression_count)
		return NULL;
	return &model->constant_expressions[expression.index];
}

const pigen_bit_state *pigen_const_expr_bits(
	const pigen_semantic_model *model, pigen_const_expr_id expression)
{
	const pigen_const_expr *known = pigen_const_expr_get(model, expression);
	if (!known || known->kind != PIGEN_CONST_EXPR_BITS ||
		known->as.bits.first_state > model->literal_state_count ||
		known->as.bits.state_count > model->literal_state_count -
			known->as.bits.first_state)
		return NULL;
	return model->literal_states + known->as.bits.first_state;
}

static pigen_expr_id add_expression(pigen_semantic_model *model,
	pigen_semantic_expr expression)
{
	pigen_expr_id result;

	if (!pigen_type_get(model, expression.type) ||
		!pigen_source_span_valid(model->sources, expression.span) ||
		(expression.constant.index != PIGEN_INVALID_ID &&
		!pigen_const_expr_get(model, expression.constant)) ||
		!id_capacity_available(model->expression_count))
		return INVALID_ID(pigen_expr_id);
	if (model->expression_count == model->expression_capacity)
	{
		model->expression_capacity = model->expression_capacity ?
			model->expression_capacity * 2 : 16;
		model->expressions = pigen_resize(model->expressions,
			model->expression_capacity * sizeof(*model->expressions));
	}
	result = (pigen_expr_id){(uint32_t)model->expression_count};
	model->expressions[model->expression_count++] = expression;
	return result;
}

pigen_expr_id pigen_expr_add_integer(pigen_semantic_model *model,
	uint64_t value, pigen_type_id type, pigen_source_span span)
{
	pigen_semantic_expr expression = {0};
	expression.kind = PIGEN_EXPR_INTEGER;
	expression.type = type;
	expression.span = span;
	expression.constant = pigen_const_expr_intern_integer(model, value, type);
	if (expression.constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.as.integer = value;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_bits(pigen_semantic_model *model,
	const pigen_bit_state *states, size_t state_count, pigen_type_id type,
	pigen_source_span span)
{
	pigen_semantic_expr expression = {0};
	pigen_const_expr_id constant = pigen_const_expr_intern_bits(model, states,
		state_count, type);

	if (constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_BITS;
	expression.type = type;
	expression.span = span;
	expression.constant = constant;
	expression.as.bits.first_state =
		pigen_const_expr_get(model, constant)->as.bits.first_state;
	expression.as.bits.state_count =
		pigen_const_expr_get(model, constant)->as.bits.state_count;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_symbol(pigen_semantic_model *model,
	pigen_symbol_id symbol, pigen_type_id type, pigen_source_span span)
{
	const pigen_symbol *known = pigen_symbol_get(model, symbol);
	pigen_semantic_expr expression = {0};
	if (!known || known->type.index != type.index ||
		(known->kind != PIGEN_SYMBOL_PARAMETER &&
		known->kind != PIGEN_SYMBOL_VALUE &&
		known->kind != PIGEN_SYMBOL_TRANSPORT))
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_SYMBOL;
	expression.type = type;
	expression.span = span;
	expression.constant = known->kind == PIGEN_SYMBOL_PARAMETER ?
		pigen_const_expr_intern_symbol(model, symbol, type) :
		INVALID_ID(pigen_const_expr_id);
	if (known->kind == PIGEN_SYMBOL_PARAMETER &&
		expression.constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.as.symbol = symbol;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_group(pigen_semantic_model *model,
	pigen_expr_id operand, pigen_source_span span)
{
	const pigen_semantic_expr *known = pigen_expr_get(model, operand);
	pigen_semantic_expr expression = {0};
	if (!known) return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_GROUP;
	expression.type = known->type;
	expression.span = span;
	expression.constant = known->constant;
	expression.as.group.operand = operand;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_unary(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_expr_id operand,
	pigen_type_id type, pigen_source_span span)
{
	const pigen_semantic_expr *known = pigen_expr_get(model, operand);
	pigen_semantic_expr expression = {0};
	if (!known || operator < PIGEN_UNARY_POSITIVE ||
		operator > PIGEN_UNARY_REDUCTION_XNOR)
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_UNARY;
	expression.type = type;
	expression.span = span;
	expression.constant = known->constant.index == PIGEN_INVALID_ID ?
		INVALID_ID(pigen_const_expr_id) :
		pigen_const_expr_intern_unary(model, operator, known->constant, type);
	if (known->constant.index != PIGEN_INVALID_ID &&
		expression.constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.as.unary.operator = operator;
	expression.as.unary.operand = operand;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_binary(pigen_semantic_model *model,
	pigen_binary_operator operator, pigen_expr_id left,
	pigen_expr_id right, pigen_type_id type, pigen_source_span span)
{
	const pigen_semantic_expr *left_expression = pigen_expr_get(model, left);
	const pigen_semantic_expr *right_expression = pigen_expr_get(model, right);
	pigen_semantic_expr expression = {0};
	if (!left_expression || !right_expression ||
		operator < PIGEN_BINARY_ADD || operator > PIGEN_BINARY_LOGICAL_OR)
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_BINARY;
	expression.type = type;
	expression.span = span;
	expression.constant = left_expression->constant.index == PIGEN_INVALID_ID ||
		right_expression->constant.index == PIGEN_INVALID_ID ?
		INVALID_ID(pigen_const_expr_id) :
		pigen_const_expr_intern_binary(model, operator,
			left_expression->constant, right_expression->constant, type);
	if (left_expression->constant.index != PIGEN_INVALID_ID &&
		right_expression->constant.index != PIGEN_INVALID_ID &&
		expression.constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.as.binary.operator = operator;
	expression.as.binary.left = left;
	expression.as.binary.right = right;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_conditional(pigen_semantic_model *model,
	pigen_expr_id condition, pigen_expr_id when_true, pigen_expr_id when_false,
	pigen_type_id type, pigen_source_span span)
{
	const pigen_semantic_expr *condition_expression =
		pigen_expr_get(model, condition);
	const pigen_semantic_expr *true_expression =
		pigen_expr_get(model, when_true);
	const pigen_semantic_expr *false_expression =
		pigen_expr_get(model, when_false);
	pigen_semantic_expr expression = {0};

	if (!condition_expression || !true_expression || !false_expression ||
		true_expression->type.index != type.index ||
		false_expression->type.index != type.index)
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_CONDITIONAL;
	expression.type = type;
	expression.span = span;
	expression.constant =
		condition_expression->constant.index == PIGEN_INVALID_ID ||
		true_expression->constant.index == PIGEN_INVALID_ID ||
		false_expression->constant.index == PIGEN_INVALID_ID ?
		INVALID_ID(pigen_const_expr_id) :
		pigen_const_expr_intern_conditional(model,
			condition_expression->constant, true_expression->constant,
			false_expression->constant, type);
	if (condition_expression->constant.index != PIGEN_INVALID_ID &&
		true_expression->constant.index != PIGEN_INVALID_ID &&
		false_expression->constant.index != PIGEN_INVALID_ID &&
		expression.constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.as.conditional.condition = condition;
	expression.as.conditional.when_true = when_true;
	expression.as.conditional.when_false = when_false;
	return add_expression(model, expression);
}

const pigen_semantic_expr *pigen_expr_get(const pigen_semantic_model *model,
	pigen_expr_id expression)
{
	if (expression.index == PIGEN_INVALID_ID ||
		expression.index >= model->expression_count)
		return NULL;
	return &model->expressions[expression.index];
}

pigen_const_expr_id pigen_expr_constant(const pigen_semantic_model *model,
	pigen_expr_id expression)
{
	const pigen_semantic_expr *known = pigen_expr_get(model, expression);
	return known ? known->constant : INVALID_ID(pigen_const_expr_id);
}

pigen_lvalue_id pigen_lvalue_resolve(pigen_semantic_model *model,
	pigen_expr_id expression)
{
	const pigen_semantic_expr *root;
	const pigen_semantic_expr *base;
	const pigen_symbol *symbol;
	pigen_transport_id transport = INVALID_ID(pigen_transport_id);
	pigen_lvalue_id result;
	size_t i;

	if (!model) return INVALID_ID(pigen_lvalue_id);
	root = pigen_expr_get(model, expression);
	if (!root) return INVALID_ID(pigen_lvalue_id);
	base = root;
	while (base && base->kind == PIGEN_EXPR_GROUP)
		base = pigen_expr_get(model, base->as.group.operand);
	if (!base || base->kind != PIGEN_EXPR_SYMBOL)
		return INVALID_ID(pigen_lvalue_id);
	symbol = pigen_symbol_get(model, base->as.symbol);
	if (!symbol || (symbol->kind != PIGEN_SYMBOL_VALUE &&
		symbol->kind != PIGEN_SYMBOL_TRANSPORT) ||
		symbol->type.index != root->type.index)
		return INVALID_ID(pigen_lvalue_id);
	if (symbol->kind == PIGEN_SYMBOL_TRANSPORT)
	{
		transport = pigen_symbol_transport(model, base->as.symbol);
		if (transport.index == PIGEN_INVALID_ID)
			return INVALID_ID(pigen_lvalue_id);
	}
	for (i = 0; i < model->lvalue_count; i++)
		if (model->lvalues[i].expression.index == expression.index)
			return (pigen_lvalue_id){(uint32_t)i};
	if (!id_capacity_available(model->lvalue_count))
		return INVALID_ID(pigen_lvalue_id);
	if (model->lvalue_count == model->lvalue_capacity)
	{
		model->lvalue_capacity = model->lvalue_capacity ?
			model->lvalue_capacity * 2 : 16;
		model->lvalues = pigen_resize(model->lvalues,
			model->lvalue_capacity * sizeof(*model->lvalues));
	}
	result = (pigen_lvalue_id){(uint32_t)model->lvalue_count};
	model->lvalues[model->lvalue_count++] = (pigen_semantic_lvalue){
		expression, root->type, base->as.symbol, transport, root->span};
	return result;
}

const pigen_semantic_lvalue *pigen_lvalue_get(
	const pigen_semantic_model *model, pigen_lvalue_id lvalue)
{
	if (!model || lvalue.index == PIGEN_INVALID_ID ||
		lvalue.index >= model->lvalue_count)
		return NULL;
	return &model->lvalues[lvalue.index];
}

pigen_scope_id pigen_scope_add(pigen_semantic_model *model,
	pigen_scope_id parent, pigen_source_span span)
{
	pigen_scope_id result;
	int span_valid = span.source.index == PIGEN_INVALID_ID ?
		span.start == 0 && span.end == 0 :
		pigen_source_span_valid(model->sources, span);

	if (!model->sources || !span_valid ||
		(parent.index != PIGEN_INVALID_ID && parent.index >= model->scope_count) ||
		!id_capacity_available(model->scope_count))
		return INVALID_ID(pigen_scope_id);
	if (model->scope_count == model->scope_capacity)
	{
		model->scope_capacity = model->scope_capacity ? model->scope_capacity * 2 : 16;
		model->scopes = pigen_resize(model->scopes,
			model->scope_capacity * sizeof(*model->scopes));
	}
	result = (pigen_scope_id){(uint32_t)model->scope_count};
	model->scopes[model->scope_count++] = (pigen_scope){parent,
		INVALID_ID(pigen_symbol_id), span};
	return result;
}

const pigen_scope *pigen_scope_get(const pigen_semantic_model *model,
	pigen_scope_id scope)
{
	if (scope.index == PIGEN_INVALID_ID || scope.index >= model->scope_count)
		return NULL;
	return &model->scopes[scope.index];
}

static pigen_symbol_id lookup_local(const pigen_semantic_model *model,
	pigen_scope_id scope, pigen_source_span name)
{
	pigen_symbol_id at = model->scopes[scope.index].last_symbol;

	while (at.index != PIGEN_INVALID_ID)
	{
		const pigen_symbol *symbol = &model->symbols[at.index];
		if (same_name(model, symbol->name, name))
			return at;
		at = symbol->previous_in_scope;
	}
	return INVALID_ID(pigen_symbol_id);
}

pigen_declare_result pigen_symbol_declare(pigen_semantic_model *model,
	pigen_scope_id scope, pigen_symbol_kind kind, pigen_type_id type,
	pigen_source_span name, pigen_source_span declaration,
	pigen_symbol_id *declared, pigen_symbol_id *shadowed)
{
	pigen_symbol_id existing;
	pigen_symbol_id result;
	pigen_scope *owner;
	pigen_symbol symbol = {0};

	if (declared) *declared = INVALID_ID(pigen_symbol_id);
	if (shadowed) *shadowed = INVALID_ID(pigen_symbol_id);
	if (!pigen_scope_get(model, scope) ||
		((kind == PIGEN_SYMBOL_VALUE || kind == PIGEN_SYMBOL_PARAMETER ||
			kind == PIGEN_SYMBOL_TYPEDEF || kind == PIGEN_SYMBOL_TRANSPORT) &&
			!pigen_type_get(model, type)) ||
		((kind == PIGEN_SYMBOL_MODULE || kind == PIGEN_SYMBOL_PIPELINE ||
			kind == PIGEN_SYMBOL_STAGE || kind == PIGEN_SYMBOL_FSM ||
			kind == PIGEN_SYMBOL_FABRIC) && type.index != PIGEN_INVALID_ID) ||
		kind < PIGEN_SYMBOL_VALUE || kind > PIGEN_SYMBOL_FABRIC ||
		!pigen_source_span_valid(model->sources, name) || name.start == name.end ||
		!pigen_source_span_valid(model->sources, declaration) ||
		!span_contains(declaration, name) || !id_capacity_available(model->symbol_count))
		return PIGEN_DECLARE_INVALID;
	existing = lookup_local(model, scope, name);
	if (existing.index != PIGEN_INVALID_ID)
	{
		if (declared) *declared = existing;
		return PIGEN_DECLARE_DUPLICATE;
	}
	owner = &model->scopes[scope.index];
	if (shadowed && owner->parent.index != PIGEN_INVALID_ID)
		*shadowed = pigen_symbol_lookup(model, owner->parent, name);
	if (model->symbol_count == model->symbol_capacity)
	{
		model->symbol_capacity = model->symbol_capacity ? model->symbol_capacity * 2 : 32;
		model->symbols = pigen_resize(model->symbols,
			model->symbol_capacity * sizeof(*model->symbols));
	}
	result = (pigen_symbol_id){(uint32_t)model->symbol_count};
	symbol.kind = kind;
	symbol.scope = scope;
	symbol.previous_in_scope = owner->last_symbol;
	symbol.type = type;
	symbol.name = name;
	symbol.declaration = declaration;
	switch (kind)
	{
		case PIGEN_SYMBOL_MODULE:
			symbol.object.module = INVALID_ID(pigen_module_id);
			break;
		case PIGEN_SYMBOL_PARAMETER:
			symbol.object.parameter = INVALID_ID(pigen_parameter_id);
			break;
		case PIGEN_SYMBOL_TRANSPORT:
			symbol.object.transport = INVALID_ID(pigen_transport_id);
			break;
		case PIGEN_SYMBOL_PIPELINE:
			symbol.object.pipeline = INVALID_ID(pigen_pipeline_id);
			break;
		case PIGEN_SYMBOL_STAGE:
			symbol.object.stage = INVALID_ID(pigen_stage_id);
			break;
		case PIGEN_SYMBOL_FSM:
			symbol.object.fsm = INVALID_ID(pigen_fsm_id);
			break;
		case PIGEN_SYMBOL_FABRIC:
			symbol.object.fabric = INVALID_ID(pigen_fabric_id);
			break;
		case PIGEN_SYMBOL_VALUE:
		case PIGEN_SYMBOL_TYPEDEF:
			symbol.object.module = INVALID_ID(pigen_module_id);
			break;
	}
	model->symbols[model->symbol_count++] = symbol;
	owner->last_symbol = result;
	if (declared) *declared = result;
	return PIGEN_DECLARE_OK;
}

pigen_symbol_id pigen_symbol_lookup(const pigen_semantic_model *model,
	pigen_scope_id scope, pigen_source_span name)
{
	while (pigen_scope_get(model, scope))
	{
		pigen_symbol_id result = lookup_local(model, scope, name);
		if (result.index != PIGEN_INVALID_ID)
			return result;
		scope = model->scopes[scope.index].parent;
	}
	return INVALID_ID(pigen_symbol_id);
}

const pigen_symbol *pigen_symbol_get(const pigen_semantic_model *model,
	pigen_symbol_id symbol)
{
	if (symbol.index == PIGEN_INVALID_ID || symbol.index >= model->symbol_count)
		return NULL;
	return &model->symbols[symbol.index];
}

pigen_module_id pigen_module_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_symbol_id symbol_id, pigen_scope_id scope,
	pigen_source_span span)
{
	pigen_symbol *symbol;
	const pigen_scope *known_scope;
	pigen_module_id result;

	symbol = symbol_id.index < model->symbol_count ?
		&model->symbols[symbol_id.index] : NULL;
	known_scope = pigen_scope_get(model, scope);
	if (!symbol || symbol->kind != PIGEN_SYMBOL_MODULE ||
		symbol->object.module.index != PIGEN_INVALID_ID ||
		symbol->scope.index != model->compilation_scope.index ||
		!known_scope ||
		known_scope->parent.index != model->compilation_scope.index ||
		!spans_equal(symbol->declaration, span) ||
		!spans_equal(known_scope->span, span) ||
		syntax.index == PIGEN_INVALID_ID ||
		!pigen_source_span_valid(model->sources, span) ||
		!id_capacity_available(model->module_count))
		return INVALID_ID(pigen_module_id);
	if (model->module_count == model->module_capacity)
	{
		model->module_capacity = model->module_capacity ?
			model->module_capacity * 2 : 8;
		model->modules = pigen_resize(model->modules,
			model->module_capacity * sizeof(*model->modules));
	}
	result = (pigen_module_id){(uint32_t)model->module_count};
	model->modules[model->module_count++] = (pigen_semantic_module){
		syntax, symbol_id, scope, span};
	symbol->object.module = result;
	return result;
}

pigen_parameter_id pigen_parameter_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module_id,
	pigen_symbol_id symbol_id, pigen_expr_id value, int is_local,
	pigen_source_span span)
{
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	const pigen_semantic_expr *expression = pigen_expr_get(model, value);
	pigen_symbol *symbol = symbol_id.index < model->symbol_count ?
		&model->symbols[symbol_id.index] : NULL;
	pigen_parameter_id result;

	if (!module || !expression || !symbol ||
		symbol->kind != PIGEN_SYMBOL_PARAMETER ||
		symbol->object.parameter.index != PIGEN_INVALID_ID ||
		symbol->scope.index != module->scope.index ||
		symbol->type.index != expression->type.index ||
		!spans_equal(symbol->declaration, span) ||
		pigen_expr_constant(model, value).index == PIGEN_INVALID_ID ||
		syntax.index == PIGEN_INVALID_ID || (is_local != 0 && is_local != 1) ||
		!pigen_source_span_valid(model->sources, span) ||
		!id_capacity_available(model->parameter_count))
		return INVALID_ID(pigen_parameter_id);
	if (model->parameter_count == model->parameter_capacity)
	{
		model->parameter_capacity = model->parameter_capacity ?
			model->parameter_capacity * 2 : 16;
		model->parameters = pigen_resize(model->parameters,
			model->parameter_capacity * sizeof(*model->parameters));
	}
	result = (pigen_parameter_id){(uint32_t)model->parameter_count};
	model->parameters[model->parameter_count++] = (pigen_semantic_parameter){
		syntax, module_id, symbol_id, value, is_local, span};
	symbol->object.parameter = result;
	return result;
}

pigen_transport_id pigen_transport_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module_id,
	pigen_symbol_id symbol_id, pigen_type_id payload_type,
	pigen_expr_id fifo_depth, pigen_semantic_transport_kind kind,
	pigen_semantic_direction direction, pigen_source_span span)
{
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_symbol *symbol = symbol_id.index < model->symbol_count ?
		&model->symbols[symbol_id.index] : NULL;
	pigen_transport_id result;
	int fifo = kind == PIGEN_SEMANTIC_FIFO;

	if (!module || !symbol || symbol->kind != PIGEN_SYMBOL_TRANSPORT ||
		symbol->object.transport.index != PIGEN_INVALID_ID ||
		symbol->scope.index != module->scope.index ||
		symbol->type.index != payload_type.index ||
		!spans_equal(symbol->declaration, span) ||
		!pigen_type_get(model, payload_type) ||
		kind < PIGEN_SEMANTIC_BUF || kind > PIGEN_SEMANTIC_FIFO ||
		direction < PIGEN_SEMANTIC_INTERNAL ||
		direction > PIGEN_SEMANTIC_INOUT ||
		(fifo && (!pigen_expr_get(model, fifo_depth) ||
			pigen_expr_constant(model, fifo_depth).index == PIGEN_INVALID_ID)) ||
		(!fifo && fifo_depth.index != PIGEN_INVALID_ID) ||
		syntax.index == PIGEN_INVALID_ID ||
		!pigen_source_span_valid(model->sources, span) ||
		!id_capacity_available(model->transport_count))
		return INVALID_ID(pigen_transport_id);
	if (model->transport_count == model->transport_capacity)
	{
		model->transport_capacity = model->transport_capacity ?
			model->transport_capacity * 2 : 32;
		model->transports = pigen_resize(model->transports,
			model->transport_capacity * sizeof(*model->transports));
	}
	result = (pigen_transport_id){(uint32_t)model->transport_count};
	model->transports[model->transport_count++] = (pigen_semantic_transport){
		syntax, module_id, symbol_id, payload_type, fifo_depth, kind, direction,
		span};
	symbol->object.transport = result;
	return result;
}

pigen_module_id pigen_symbol_module(const pigen_semantic_model *model,
	pigen_symbol_id symbol_id)
{
	const pigen_symbol *symbol = pigen_symbol_get(model, symbol_id);
	const pigen_semantic_module *module;
	if (!symbol || symbol->kind != PIGEN_SYMBOL_MODULE)
		return INVALID_ID(pigen_module_id);
	module = pigen_module_get(model, symbol->object.module);
	return module && module->symbol.index == symbol_id.index ?
		symbol->object.module : INVALID_ID(pigen_module_id);
}

pigen_parameter_id pigen_symbol_parameter(const pigen_semantic_model *model,
	pigen_symbol_id symbol_id)
{
	const pigen_symbol *symbol = pigen_symbol_get(model, symbol_id);
	const pigen_semantic_parameter *parameter;
	if (!symbol || symbol->kind != PIGEN_SYMBOL_PARAMETER)
		return INVALID_ID(pigen_parameter_id);
	parameter = pigen_parameter_get(model, symbol->object.parameter);
	return parameter && parameter->symbol.index == symbol_id.index ?
		symbol->object.parameter : INVALID_ID(pigen_parameter_id);
}

pigen_transport_id pigen_symbol_transport(const pigen_semantic_model *model,
	pigen_symbol_id symbol_id)
{
	const pigen_symbol *symbol = pigen_symbol_get(model, symbol_id);
	const pigen_semantic_transport *transport;
	if (!symbol || symbol->kind != PIGEN_SYMBOL_TRANSPORT)
		return INVALID_ID(pigen_transport_id);
	transport = pigen_transport_get(model, symbol->object.transport);
	return transport && transport->symbol.index == symbol_id.index ?
		symbol->object.transport : INVALID_ID(pigen_transport_id);
}

const pigen_semantic_module *pigen_module_get(const pigen_semantic_model *model,
	pigen_module_id module)
{
	if (module.index == PIGEN_INVALID_ID || module.index >= model->module_count)
		return NULL;
	return &model->modules[module.index];
}

const pigen_semantic_parameter *pigen_parameter_get(
	const pigen_semantic_model *model, pigen_parameter_id parameter)
{
	if (parameter.index == PIGEN_INVALID_ID ||
		parameter.index >= model->parameter_count)
		return NULL;
	return &model->parameters[parameter.index];
}

const pigen_semantic_transport *pigen_transport_get(
	const pigen_semantic_model *model, pigen_transport_id transport)
{
	if (transport.index == PIGEN_INVALID_ID ||
		transport.index >= model->transport_count)
		return NULL;
	return &model->transports[transport.index];
}

void pigen_free_semantic_model(pigen_semantic_model *model)
{
	free(model->types);
	free(model->dimensions);
	free(model->scopes);
	free(model->symbols);
	free(model->expressions);
	free(model->constant_expressions);
	free(model->literal_states);
	free(model->predicates);
	free(model->predicate_atoms);
	free(model->lvalues);
	free(model->modules);
	free(model->parameters);
	free(model->transports);
	*model = (pigen_semantic_model){0};
}
