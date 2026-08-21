/* Stable semantic identities, lexical scopes, symbols, and structured types. */
#include <stdlib.h>
#include <string.h>

#include "pigen/semantic.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

const pigen_transfer_type_laws *pigen_transfer_type_get(
	pigen_transfer_type transfer_type)
{
	static const pigen_transfer_type_laws laws[] = {
		[PIGEN_TRANSFER_TYPE_ABSTRACT] = {0, -1, -1, -1, -1, -1, -1},
		[PIGEN_TRANSFER_TYPE_WIRE] = {1, 1, 0, 0, 0, 0, 0},
		[PIGEN_TRANSFER_TYPE_REG] = {1, 1, 1, 0, 0, 0, 0},
		[PIGEN_TRANSFER_TYPE_LOGIC] = {1, 1, 1, 0, 0, 0, 0},
		[PIGEN_TRANSFER_TYPE_BUF] = {0, -1, -1, 1, 1, 1, 1},
		[PIGEN_TRANSFER_TYPE_PORT] = {0, -1, -1, 1, 1, 1, 1},
		[PIGEN_TRANSFER_TYPE_FIFO] = {0, -1, -1, 1, 1, 1, 1},
		[PIGEN_TRANSFER_TYPE_SKID] = {0, -1, -1, 1, 1, 1, 1}
	};

	if ((size_t)transfer_type >= sizeof(laws) / sizeof(laws[0])) return NULL;
	return &laws[transfer_type];
}

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

void pigen_semantic_init(pigen_semantic_model *model,
	const pigen_source_manager *sources)
{
	*model = (pigen_semantic_model){0};
	model->sources = sources;
	model->compilation_scope = INVALID_ID(pigen_scope_id);
	model->integer_data_type = INVALID_ID(pigen_data_type_id);
	model->boolean_data_type = INVALID_ID(pigen_data_type_id);
	model->scalar_shape = INVALID_ID(pigen_shape_id);
	model->true_predicate = INVALID_ID(pigen_predicate_id);
	model->false_predicate = INVALID_ID(pigen_predicate_id);
}

static int shape_dimensions_equal(const pigen_semantic_model *model,
	const pigen_semantic_shape *shape,
	const pigen_shape_dimension *dimensions, size_t dimension_count)
{
	size_t i;

	if (shape->dimension_count != dimension_count) return 0;
	for (i = 0; i < dimension_count; i++)
	{
		const pigen_shape_dimension *known =
			&model->shape_dimensions[shape->first_dimension + i];
		if (known->form != dimensions[i].form) return 0;
		if (known->form == PIGEN_SHAPE_DIMENSION_COUNT)
		{
			if (known->as.count.index != dimensions[i].as.count.index)
				return 0;
		}
		else if (known->as.range.left.index !=
			dimensions[i].as.range.left.index ||
			known->as.range.right.index !=
			dimensions[i].as.range.right.index)
			return 0;
	}
	return 1;
}

pigen_shape_id pigen_shape_intern(pigen_semantic_model *model,
	const pigen_shape_dimension *dimensions, size_t dimension_count)
{
	pigen_shape_id result;
	size_t i;
	size_t needed;

	if (!model || (dimension_count && !dimensions) ||
		dimension_count > SIZE_MAX - model->shape_dimension_count ||
		!id_capacity_available(model->shape_count))
		return INVALID_ID(pigen_shape_id);
	for (i = 0; i < dimension_count; i++)
	{
		if (dimensions[i].form == PIGEN_SHAPE_DIMENSION_COUNT)
		{
			if (!pigen_const_expr_get(model, dimensions[i].as.count))
				return INVALID_ID(pigen_shape_id);
		}
		else if (dimensions[i].form == PIGEN_SHAPE_DIMENSION_RANGE)
		{
			if (!pigen_const_expr_get(model, dimensions[i].as.range.left) ||
				!pigen_const_expr_get(model, dimensions[i].as.range.right))
				return INVALID_ID(pigen_shape_id);
		}
		else
			return INVALID_ID(pigen_shape_id);
	}
	for (i = 0; i < model->shape_count; i++)
		if (shape_dimensions_equal(model, &model->shapes[i], dimensions,
			dimension_count))
			return (pigen_shape_id){(uint32_t)i};
	if (model->shape_count == model->shape_capacity)
	{
		model->shape_capacity = model->shape_capacity ?
			model->shape_capacity * 2 : 16;
		model->shapes = pigen_resize(model->shapes,
			model->shape_capacity * sizeof(*model->shapes));
	}
	needed = model->shape_dimension_count + dimension_count;
	if (needed > model->shape_dimension_capacity)
	{
		size_t capacity = model->shape_dimension_capacity ?
			model->shape_dimension_capacity * 2 : 16;
		while (capacity < needed) capacity *= 2;
		model->shape_dimensions = pigen_resize(model->shape_dimensions,
			capacity * sizeof(*model->shape_dimensions));
		model->shape_dimension_capacity = capacity;
	}
	result = (pigen_shape_id){(uint32_t)model->shape_count};
	model->shapes[model->shape_count++] = (pigen_semantic_shape){
		model->shape_dimension_count, dimension_count};
	if (dimension_count)
		memcpy(model->shape_dimensions + model->shape_dimension_count,
			dimensions, dimension_count * sizeof(*dimensions));
	model->shape_dimension_count = needed;
	return result;
}

pigen_shape_id pigen_semantic_scalar_shape(pigen_semantic_model *model)
{
	if (model && model->scalar_shape.index == PIGEN_INVALID_ID)
		model->scalar_shape = pigen_shape_intern(model, NULL, 0);
	return model ? model->scalar_shape : INVALID_ID(pigen_shape_id);
}

const pigen_semantic_shape *pigen_shape_get(
	const pigen_semantic_model *model, pigen_shape_id shape)
{
	if (!model || shape.index == PIGEN_INVALID_ID ||
		shape.index >= model->shape_count) return NULL;
	return &model->shapes[shape.index];
}

const pigen_shape_dimension *pigen_shape_dimensions(
	const pigen_semantic_model *model, pigen_shape_id shape)
{
	const pigen_semantic_shape *known = pigen_shape_get(model, shape);
	if (!known || !known->dimension_count) return NULL;
	return model->shape_dimensions + known->first_dimension;
}

pigen_shape_id pigen_shape_element(pigen_semantic_model *model,
	pigen_shape_id shape)
{
	const pigen_semantic_shape *known = pigen_shape_get(model, shape);
	const pigen_shape_dimension *dimensions = pigen_shape_dimensions(model,
		shape);

	if (!known || !known->dimension_count || !dimensions)
		return INVALID_ID(pigen_shape_id);
	return pigen_shape_intern(model, dimensions + 1,
		known->dimension_count - 1);
}

static int const_expressions_equal(const pigen_semantic_model *model,
	const pigen_const_expr *left, const pigen_const_expr *right)
{
	if (left->kind != right->kind || left->data_type.index != right->data_type.index)
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
		case PIGEN_CONST_EXPR_INDEX:
			return left->as.index.base.index == right->as.index.base.index &&
				left->as.index.index.index == right->as.index.index.index;
		case PIGEN_CONST_EXPR_SELECT:
			return left->as.select.base.index ==
					right->as.select.base.index &&
				left->as.select.left.index ==
					right->as.select.left.index &&
				left->as.select.right.index ==
					right->as.select.right.index &&
				left->as.select.kind == right->as.select.kind;
		case PIGEN_CONST_EXPR_SELECT_WIDTH:
			return left->as.select_width.left.index ==
					right->as.select_width.left.index &&
				left->as.select_width.right.index ==
					right->as.select_width.right.index &&
				left->as.select_width.kind == right->as.select_width.kind;
		case PIGEN_CONST_EXPR_CONCATENATION:
		case PIGEN_CONST_EXPR_WIDTH_SUM:
		case PIGEN_CONST_EXPR_WIDTH_PRODUCT:
			return left->as.sequence.child_count ==
					right->as.sequence.child_count &&
				!memcmp(model->constant_expression_children +
						left->as.sequence.first_child,
					model->constant_expression_children +
						right->as.sequence.first_child,
					left->as.sequence.child_count *
						sizeof(*model->constant_expression_children));
	}
	return 0;
}

static pigen_const_expr_id intern_const_expression(
	pigen_semantic_model *model, pigen_const_expr expression)
{
	size_t i;
	pigen_const_expr_id result;

	if (!pigen_data_type_exists(model, expression.data_type) ||
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
	pigen_semantic_model *model, uint64_t value, pigen_data_type_id type)
{
	pigen_const_expr expression = {0};
	expression.kind = PIGEN_CONST_EXPR_INTEGER;
	expression.data_type = type;
	expression.as.integer = value;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_bits(pigen_semantic_model *model,
	const pigen_bit_state *states, size_t state_count, pigen_data_type_id type)
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
	expression.data_type = type;
	expression.as.bits.first_state = model->literal_state_count;
	expression.as.bits.state_count = state_count;
	for (i = 0; i < model->constant_expression_count; i++)
		if (model->constant_expressions[i].kind == PIGEN_CONST_EXPR_BITS &&
			model->constant_expressions[i].data_type.index == type.index &&
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
	pigen_semantic_model *model, pigen_symbol_id symbol, pigen_data_type_id type)
{
	const pigen_symbol *known = pigen_symbol_get(model, symbol);
	pigen_const_expr expression = {0};

	if (!known || known->kind != PIGEN_SYMBOL_PARAMETER ||
		known->data_type.index != type.index)
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_SYMBOL;
	expression.data_type = type;
	expression.as.symbol = symbol;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_unary(
	pigen_semantic_model *model, pigen_unary_operator operator,
	pigen_const_expr_id operand, pigen_data_type_id type)
{
	pigen_const_expr expression = {0};

	if (operator < PIGEN_UNARY_POSITIVE ||
		operator > PIGEN_UNARY_REDUCTION_XNOR ||
		!pigen_const_expr_get(model, operand))
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_UNARY;
	expression.data_type = type;
	expression.as.unary.operator = operator;
	expression.as.unary.operand = operand;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_binary(
	pigen_semantic_model *model, pigen_binary_operator operator,
	pigen_const_expr_id left, pigen_const_expr_id right, pigen_data_type_id type)
{
	pigen_const_expr expression = {0};

	if (operator < PIGEN_BINARY_ADD ||
		operator > PIGEN_BINARY_LOGICAL_OR ||
		!pigen_const_expr_get(model, left) ||
		!pigen_const_expr_get(model, right))
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_BINARY;
	expression.data_type = type;
	expression.as.binary.operator = operator;
	expression.as.binary.left = left;
	expression.as.binary.right = right;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_conditional(
	pigen_semantic_model *model, pigen_const_expr_id condition,
	pigen_const_expr_id when_true, pigen_const_expr_id when_false,
	pigen_data_type_id type)
{
	const pigen_const_expr *condition_expression =
		pigen_const_expr_get(model, condition);
	const pigen_const_expr *true_expression =
		pigen_const_expr_get(model, when_true);
	const pigen_const_expr *false_expression =
		pigen_const_expr_get(model, when_false);
	pigen_const_expr expression = {0};

	if (!condition_expression || !true_expression || !false_expression ||
		true_expression->data_type.index != type.index ||
		false_expression->data_type.index != type.index)
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_CONDITIONAL;
	expression.data_type = type;
	expression.as.conditional.condition = condition;
	expression.as.conditional.when_true = when_true;
	expression.as.conditional.when_false = when_false;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_index(
	pigen_semantic_model *model, pigen_const_expr_id base,
	pigen_const_expr_id index, pigen_data_type_id type)
{
	const pigen_const_expr *base_expression =
		pigen_const_expr_get(model, base);
	const pigen_const_expr *index_expression =
		pigen_const_expr_get(model, index);
	pigen_const_expr expression = {0};

	if (!base_expression || !index_expression ||
		pigen_data_type_packed_element(model, base_expression->data_type).index !=
			type.index)
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_INDEX;
	expression.data_type = type;
	expression.as.index.base = base;
	expression.as.index.index = index;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_select_width(
	pigen_semantic_model *model, pigen_const_expr_id left,
	pigen_const_expr_id right, pigen_select_kind kind)
{
	pigen_const_expr expression = {0};

	if (!model || !pigen_select_kind_is_valid(kind) ||
		!pigen_const_expr_get(model, right) ||
		(kind == PIGEN_SEMANTIC_SELECT_RANGE) !=
			(left.index != PIGEN_INVALID_ID) ||
		(left.index != PIGEN_INVALID_ID &&
		!pigen_const_expr_get(model, left)))
		return INVALID_ID(pigen_const_expr_id);
	if (kind == PIGEN_SEMANTIC_SELECT_RANGE && left.index > right.index)
	{
		pigen_const_expr_id swap = left;
		left = right;
		right = swap;
	}
	else if (kind != PIGEN_SEMANTIC_SELECT_RANGE)
		kind = PIGEN_SEMANTIC_SELECT_INDEXED_UP;
	expression.kind = PIGEN_CONST_EXPR_SELECT_WIDTH;
	expression.data_type = pigen_data_type_integer(model);
	expression.as.select_width.left = left;
	expression.as.select_width.right = right;
	expression.as.select_width.kind = kind;
	return intern_const_expression(model, expression);
}

pigen_const_expr_id pigen_const_expr_intern_select(
	pigen_semantic_model *model, pigen_const_expr_id base,
	pigen_const_expr_id left, pigen_const_expr_id right,
	pigen_select_kind kind, pigen_data_type_id type)
{
	const pigen_const_expr *base_expression =
		pigen_const_expr_get(model, base);
	pigen_const_expr expression = {0};
	pigen_const_expr_id type_left =
		kind == PIGEN_SEMANTIC_SELECT_RANGE ?
			left : INVALID_ID(pigen_const_expr_id);

	if (!base_expression || !pigen_const_expr_get(model, left) ||
		!pigen_const_expr_get(model, right) ||
		!pigen_select_kind_is_valid(kind) ||
		pigen_data_type_packed_select(model, base_expression->data_type,
			type_left, right, kind).index != type.index)
		return INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_CONST_EXPR_SELECT;
	expression.data_type = type;
	expression.as.select.base = base;
	expression.as.select.left = left;
	expression.as.select.right = right;
	expression.as.select.kind = kind;
	return intern_const_expression(model, expression);
}

const pigen_const_expr_id *pigen_const_expr_children(
	const pigen_semantic_model *model, size_t first, size_t count)
{
	if (!model || first > model->constant_expression_child_count ||
		count > model->constant_expression_child_count - first)
		return NULL;
	return model->constant_expression_children + first;
}

static size_t append_const_children(pigen_semantic_model *model,
	const pigen_const_expr_id *children, size_t count)
{
	size_t first = model->constant_expression_child_count;
	size_t needed;
	size_t capacity;

	if (!count || !children ||
		count > SIZE_MAX - model->constant_expression_child_count)
		return SIZE_MAX;
	needed = model->constant_expression_child_count + count;
	if (needed > model->constant_expression_child_capacity)
	{
		capacity = model->constant_expression_child_capacity ?
			model->constant_expression_child_capacity * 2 : 32;
		while (capacity < needed) capacity *= 2;
		model->constant_expression_children = pigen_resize(
			model->constant_expression_children,
			capacity * sizeof(*model->constant_expression_children));
		model->constant_expression_child_capacity = capacity;
	}
	memcpy(model->constant_expression_children +
			model->constant_expression_child_count,
		children, count * sizeof(*children));
	model->constant_expression_child_count = needed;
	return first;
}

static pigen_const_expr_id intern_const_sequence(
	pigen_semantic_model *model, pigen_const_expr_kind kind,
	const pigen_const_expr_id *children, size_t count, pigen_data_type_id type)
{
	pigen_const_expr expression = {0};
	pigen_const_expr_id result;
	size_t before;
	size_t first;
	size_t i;

	if (!model || !count || !children ||
		(kind != PIGEN_CONST_EXPR_CONCATENATION &&
		kind != PIGEN_CONST_EXPR_WIDTH_SUM &&
		kind != PIGEN_CONST_EXPR_WIDTH_PRODUCT))
		return INVALID_ID(pigen_const_expr_id);
	for (i = 0; i < count; i++)
		if (!pigen_const_expr_get(model, children[i]))
			return INVALID_ID(pigen_const_expr_id);
	first = append_const_children(model, children, count);
	if (first == SIZE_MAX) return INVALID_ID(pigen_const_expr_id);
	expression.kind = kind;
	expression.data_type = type;
	expression.as.sequence.first_child = first;
	expression.as.sequence.child_count = count;
	before = model->constant_expression_count;
	result = intern_const_expression(model, expression);
	if (result.index == PIGEN_INVALID_ID ||
		model->constant_expression_count == before)
		model->constant_expression_child_count = first;
	return result;
}

static int const_id_compare(const void *left, const void *right)
{
	const pigen_const_expr_id *left_id = left;
	const pigen_const_expr_id *right_id = right;
	return left_id->index < right_id->index ? -1 :
		left_id->index > right_id->index;
}

static int collect_width_terms(const pigen_semantic_model *model,
	pigen_const_expr_kind kind, pigen_const_expr_id expression,
	pigen_const_expr_id **terms, size_t *count, size_t *capacity)
{
	const pigen_const_expr *known = pigen_const_expr_get(model, expression);
	const pigen_const_expr_id *children;
	size_t i;

	if (!known) return 0;
	if (known->kind == kind)
	{
		children = pigen_const_expr_children(model,
			known->as.sequence.first_child, known->as.sequence.child_count);
		if (!children) return 0;
		for (i = 0; i < known->as.sequence.child_count; i++)
			if (!collect_width_terms(model, kind, children[i],
				terms, count, capacity))
				return 0;
		return 1;
	}
	if (known->kind == PIGEN_CONST_EXPR_INTEGER &&
		((kind == PIGEN_CONST_EXPR_WIDTH_SUM && known->as.integer == 0) ||
		(kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT &&
		known->as.integer == 1)))
		return 1;
	if (*count == *capacity)
	{
		*capacity = *capacity ? *capacity * 2 : 8;
		*terms = pigen_resize(*terms, *capacity * sizeof(**terms));
	}
	(*terms)[(*count)++] = expression;
	return 1;
}

static pigen_const_expr_id intern_width_sequence(
	pigen_semantic_model *model, pigen_const_expr_kind kind,
	const pigen_const_expr_id *children, size_t count)
{
	pigen_const_expr_id *terms = NULL;
	pigen_const_expr_id result;
	pigen_data_type_id integer_type = pigen_data_type_integer(model);
	size_t term_count = 0;
	size_t capacity = 0;
	size_t i;

	if (!count || !children ||
		(kind != PIGEN_CONST_EXPR_WIDTH_SUM &&
		kind != PIGEN_CONST_EXPR_WIDTH_PRODUCT))
		return INVALID_ID(pigen_const_expr_id);
	for (i = 0; i < count; i++)
		if (!collect_width_terms(model, kind, children[i],
			&terms, &term_count, &capacity))
		{
			free(terms);
			return INVALID_ID(pigen_const_expr_id);
		}
	if (!term_count)
		result = pigen_const_expr_intern_integer(model,
			kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT ? 1 : 0, integer_type);
	else if (term_count == 1)
		result = terms[0];
	else
	{
		qsort(terms, term_count, sizeof(*terms), const_id_compare);
		result = intern_const_sequence(model, kind, terms, term_count,
			integer_type);
	}
	free(terms);
	return result;
}

pigen_const_expr_id pigen_const_expr_intern_width_sum(
	pigen_semantic_model *model, const pigen_const_expr_id *terms,
	size_t count)
{
	return intern_width_sequence(model, PIGEN_CONST_EXPR_WIDTH_SUM,
		terms, count);
}

pigen_const_expr_id pigen_const_expr_intern_width_product(
	pigen_semantic_model *model, const pigen_const_expr_id *factors,
	size_t count)
{
	return intern_width_sequence(model, PIGEN_CONST_EXPR_WIDTH_PRODUCT,
		factors, count);
}

pigen_const_expr_id pigen_const_expr_intern_concatenation(
	pigen_semantic_model *model, const pigen_const_expr_id *children,
	size_t count, pigen_data_type_id type)
{
	pigen_data_type_id *data_types;
	pigen_const_expr_id result;
	size_t i;

	if (!model || !children || !count) return INVALID_ID(pigen_const_expr_id);
	data_types = pigen_resize(NULL, count * sizeof(*data_types));
	for (i = 0; i < count; i++)
	{
		const pigen_const_expr *child =
			pigen_const_expr_get(model, children[i]);
		if (!child)
		{
			free(data_types);
			return INVALID_ID(pigen_const_expr_id);
		}
		data_types[i] = child->data_type;
	}
	if (pigen_data_type_concatenation(model, data_types, count).index !=
		type.index)
	{
		free(data_types);
		return INVALID_ID(pigen_const_expr_id);
	}
	free(data_types);
	result = intern_const_sequence(model, PIGEN_CONST_EXPR_CONCATENATION,
		children, count, type);
	return result;
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

	expression.lvalue = INVALID_ID(pigen_lvalue_id);
	if (!pigen_data_type_exists(model, expression.data_type) ||
		!pigen_shape_get(model, expression.shape) ||
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
	uint64_t value, pigen_data_type_id type, pigen_source_span span)
{
	pigen_semantic_expr expression = {0};
	expression.kind = PIGEN_EXPR_INTEGER;
	expression.data_type = type;
	expression.shape = pigen_semantic_scalar_shape(model);
	expression.span = span;
	expression.constant = pigen_const_expr_intern_integer(model, value, type);
	if (expression.constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.as.integer = value;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_bits(pigen_semantic_model *model,
	const pigen_bit_state *states, size_t state_count, pigen_data_type_id type,
	pigen_source_span span)
{
	pigen_semantic_expr expression = {0};
	pigen_const_expr_id constant = pigen_const_expr_intern_bits(model, states,
		state_count, type);

	if (constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_BITS;
	expression.data_type = type;
	expression.shape = pigen_semantic_scalar_shape(model);
	expression.span = span;
	expression.constant = constant;
	expression.as.bits.first_state =
		pigen_const_expr_get(model, constant)->as.bits.first_state;
	expression.as.bits.state_count =
		pigen_const_expr_get(model, constant)->as.bits.state_count;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_symbol(pigen_semantic_model *model,
	pigen_symbol_id symbol, pigen_data_type_id type, pigen_source_span span)
{
	const pigen_symbol *known = pigen_symbol_get(model, symbol);
	const pigen_semantic_signal *signal;
	pigen_semantic_expr expression = {0};
	if (!known || known->data_type.index != type.index ||
		(known->kind != PIGEN_SYMBOL_PARAMETER &&
		known->kind != PIGEN_SYMBOL_SIGNAL))
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_SYMBOL;
	expression.data_type = type;
	signal = known->kind == PIGEN_SYMBOL_SIGNAL ? pigen_signal_get(model,
		pigen_symbol_signal(model, symbol)) : NULL;
	if (known->kind == PIGEN_SYMBOL_SIGNAL && !signal)
		return INVALID_ID(pigen_expr_id);
	expression.shape = signal ? signal->shape :
		pigen_semantic_scalar_shape(model);
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
	expression.data_type = known->data_type;
	expression.shape = known->shape;
	expression.span = span;
	expression.constant = known->constant;
	expression.as.group.operand = operand;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_unary(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_expr_id operand,
	pigen_data_type_id type, pigen_source_span span)
{
	const pigen_semantic_expr *known = pigen_expr_get(model, operand);
	pigen_semantic_expr expression = {0};
	if (!known || operator < PIGEN_UNARY_POSITIVE ||
		operator > PIGEN_UNARY_REDUCTION_XNOR)
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_UNARY;
	expression.data_type = type;
	expression.shape = known->shape;
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
	pigen_expr_id right, pigen_data_type_id type, pigen_source_span span)
{
	const pigen_semantic_expr *left_expression = pigen_expr_get(model, left);
	const pigen_semantic_expr *right_expression = pigen_expr_get(model, right);
	pigen_semantic_expr expression = {0};
	if (!left_expression || !right_expression ||
		left_expression->shape.index != right_expression->shape.index ||
		operator < PIGEN_BINARY_ADD || operator > PIGEN_BINARY_LOGICAL_OR)
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_BINARY;
	expression.data_type = type;
	expression.shape = left_expression->shape;
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
	pigen_data_type_id type, pigen_source_span span)
{
	const pigen_semantic_expr *condition_expression =
		pigen_expr_get(model, condition);
	const pigen_semantic_expr *true_expression =
		pigen_expr_get(model, when_true);
	const pigen_semantic_expr *false_expression =
		pigen_expr_get(model, when_false);
	pigen_semantic_expr expression = {0};

	if (!condition_expression || !true_expression || !false_expression ||
		true_expression->data_type.index != type.index ||
		false_expression->data_type.index != type.index ||
		true_expression->shape.index != false_expression->shape.index)
		return INVALID_ID(pigen_expr_id);
	expression.kind = PIGEN_EXPR_CONDITIONAL;
	expression.data_type = type;
	expression.shape = true_expression->shape;
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

pigen_expr_id pigen_expr_add_index(pigen_semantic_model *model,
	pigen_expr_id base, pigen_expr_id index, pigen_source_span span)
{
	const pigen_semantic_expr *base_expression = pigen_expr_get(model, base);
	const pigen_semantic_expr *index_expression =
		pigen_expr_get(model, index);
	pigen_const_expr_id base_constant;
	pigen_const_expr_id index_constant;
	pigen_semantic_expr expression = {0};
	const pigen_semantic_shape *base_shape;
	const pigen_semantic_shape *index_shape;

	if (!base_expression || !index_expression)
		return INVALID_ID(pigen_expr_id);
	base_shape = pigen_shape_get(model, base_expression->shape);
	index_shape = pigen_shape_get(model, index_expression->shape);
	if (!base_shape || !index_shape || index_shape->dimension_count)
		return INVALID_ID(pigen_expr_id);
	base_constant = base_expression->constant;
	index_constant = index_expression->constant;
	expression.kind = PIGEN_EXPR_INDEX;
	if (base_shape->dimension_count)
	{
		expression.data_type = base_expression->data_type;
		expression.shape = pigen_shape_element(model, base_expression->shape);
	}
	else
	{
		expression.data_type = pigen_data_type_packed_element(model,
			base_expression->data_type);
		expression.shape = base_expression->shape;
	}
	if (expression.data_type.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.span = span;
	expression.constant = base_shape->dimension_count ?
		INVALID_ID(pigen_const_expr_id) :
		base_constant.index == PIGEN_INVALID_ID ||
		index_constant.index == PIGEN_INVALID_ID ?
		INVALID_ID(pigen_const_expr_id) :
		pigen_const_expr_intern_index(model, base_constant, index_constant,
			expression.data_type);
	if (base_constant.index != PIGEN_INVALID_ID &&
		index_constant.index != PIGEN_INVALID_ID &&
		expression.constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.as.index.base = base;
	expression.as.index.index = index;
	return add_expression(model, expression);
}

pigen_expr_id pigen_expr_add_select(pigen_semantic_model *model,
	pigen_expr_id base, pigen_expr_id left, pigen_expr_id right,
	pigen_select_kind kind, pigen_source_span span)
{
	const pigen_semantic_expr *base_expression = pigen_expr_get(model, base);
	const pigen_semantic_expr *left_expression = pigen_expr_get(model, left);
	const pigen_semantic_expr *right_expression = pigen_expr_get(model, right);
	const pigen_semantic_shape *base_shape;
	const pigen_semantic_shape *left_shape;
	const pigen_semantic_shape *right_shape;
	pigen_const_expr_id base_constant;
	pigen_const_expr_id left_constant;
	pigen_const_expr_id right_constant;
	pigen_const_expr_id type_left;
	pigen_semantic_expr expression = {0};

	if (!base_expression || !left_expression || !right_expression ||
		!pigen_select_kind_is_valid(kind))
		return INVALID_ID(pigen_expr_id);
	base_shape = pigen_shape_get(model, base_expression->shape);
	left_shape = pigen_shape_get(model, left_expression->shape);
	right_shape = pigen_shape_get(model, right_expression->shape);
	if (!base_shape || !left_shape || !right_shape ||
		base_shape->dimension_count || left_shape->dimension_count ||
		right_shape->dimension_count)
		return INVALID_ID(pigen_expr_id);
	base_constant = base_expression->constant;
	left_constant = left_expression->constant;
	right_constant = right_expression->constant;
	if (right_constant.index == PIGEN_INVALID_ID ||
		(kind == PIGEN_SEMANTIC_SELECT_RANGE &&
		left_constant.index == PIGEN_INVALID_ID))
		return INVALID_ID(pigen_expr_id);
	type_left = kind == PIGEN_SEMANTIC_SELECT_RANGE ?
		left_constant : INVALID_ID(pigen_const_expr_id);
	expression.kind = PIGEN_EXPR_SELECT;
	expression.data_type = pigen_data_type_packed_select(model, base_expression->data_type,
		type_left, right_constant, kind);
	expression.shape = base_expression->shape;
	if (expression.data_type.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.span = span;
	expression.constant =
		base_constant.index == PIGEN_INVALID_ID ||
		left_constant.index == PIGEN_INVALID_ID ?
		INVALID_ID(pigen_const_expr_id) :
		pigen_const_expr_intern_select(model, base_constant, left_constant,
			right_constant, kind, expression.data_type);
	if (base_constant.index != PIGEN_INVALID_ID &&
		left_constant.index != PIGEN_INVALID_ID &&
		expression.constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	expression.as.select.base = base;
	expression.as.select.left = left;
	expression.as.select.right = right;
	expression.as.select.kind = kind;
	return add_expression(model, expression);
}

const pigen_expr_id *pigen_expr_children(
	const pigen_semantic_model *model, size_t first, size_t count)
{
	if (!model || first > model->expression_child_count ||
		count > model->expression_child_count - first)
		return NULL;
	return model->expression_children + first;
}

static size_t append_expression_children(pigen_semantic_model *model,
	const pigen_expr_id *children, size_t count)
{
	size_t first = model->expression_child_count;
	size_t needed;
	size_t capacity;

	if (!count || !children || count > SIZE_MAX - model->expression_child_count)
		return SIZE_MAX;
	needed = model->expression_child_count + count;
	if (needed > model->expression_child_capacity)
	{
		capacity = model->expression_child_capacity ?
			model->expression_child_capacity * 2 : 32;
		while (capacity < needed) capacity *= 2;
		model->expression_children = pigen_resize(model->expression_children,
			capacity * sizeof(*model->expression_children));
		model->expression_child_capacity = capacity;
	}
	memcpy(model->expression_children + model->expression_child_count,
		children, count * sizeof(*children));
	model->expression_child_count = needed;
	return first;
}

pigen_expr_id pigen_expr_add_concatenation(pigen_semantic_model *model,
	const pigen_expr_id *children, size_t count, pigen_source_span span)
{
	pigen_data_type_id *data_types;
	pigen_const_expr_id *constants;
	pigen_semantic_expr expression = {0};
	pigen_expr_id result;
	size_t first;
	size_t i;
	int all_constant = 1;

	if (!model || !children || !count) return INVALID_ID(pigen_expr_id);
	data_types = pigen_resize(NULL, count * sizeof(*data_types));
	constants = pigen_resize(NULL, count * sizeof(*constants));
	for (i = 0; i < count; i++)
	{
		const pigen_semantic_expr *child = pigen_expr_get(model, children[i]);
		const pigen_semantic_shape *shape;
		if (!child || !(shape = pigen_shape_get(model, child->shape)) ||
			shape->dimension_count)
		{
			free(data_types);
			free(constants);
			return INVALID_ID(pigen_expr_id);
		}
		data_types[i] = child->data_type;
		constants[i] = child->constant;
		if (constants[i].index == PIGEN_INVALID_ID) all_constant = 0;
	}
	expression.kind = PIGEN_EXPR_CONCATENATION;
	expression.data_type = pigen_data_type_concatenation(model, data_types,
		count);
	expression.shape = pigen_semantic_scalar_shape(model);
	free(data_types);
	if (expression.data_type.index == PIGEN_INVALID_ID)
	{
		free(constants);
		return INVALID_ID(pigen_expr_id);
	}
	expression.span = span;
	expression.constant = all_constant ?
		pigen_const_expr_intern_concatenation(model, constants, count,
			expression.data_type) :
		INVALID_ID(pigen_const_expr_id);
	free(constants);
	if (all_constant && expression.constant.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_expr_id);
	first = append_expression_children(model, children, count);
	if (first == SIZE_MAX) return INVALID_ID(pigen_expr_id);
	expression.as.sequence.first_child = first;
	expression.as.sequence.child_count = count;
	result = add_expression(model, expression);
	if (result.index == PIGEN_INVALID_ID)
		model->expression_child_count = first;
	return result;
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

static pigen_lvalue_id find_lvalue(
	const pigen_semantic_model *model, pigen_expr_id expression)
{
	const pigen_semantic_expr *known = pigen_expr_get(model, expression);
	const pigen_semantic_lvalue *lvalue;
	if (!known) return INVALID_ID(pigen_lvalue_id);
	lvalue = pigen_lvalue_get(model, known->lvalue);
	if (lvalue && lvalue->expression.index == expression.index)
		return known->lvalue;
	return INVALID_ID(pigen_lvalue_id);
}

static pigen_lvalue_id add_lvalue(pigen_semantic_model *model,
	pigen_semantic_lvalue lvalue)
{
	const pigen_symbol *symbol;
	pigen_semantic_expr *expression;
	pigen_lvalue_id result;
	if (!pigen_expr_get(model, lvalue.expression) ||
		!pigen_data_type_exists(model, lvalue.data_type) ||
		!pigen_source_span_valid(model->sources, lvalue.span) ||
		!id_capacity_available(model->lvalue_count))
		return INVALID_ID(pigen_lvalue_id);
	if (lvalue.kind == PIGEN_LVALUE_PROJECTION)
	{
		symbol = pigen_symbol_get(model, lvalue.as.projection.base_symbol);
		if (!symbol || symbol->kind != PIGEN_SYMBOL_SIGNAL ||
			pigen_symbol_signal(model,
				lvalue.as.projection.base_symbol).index !=
				lvalue.as.projection.signal.index)
			return INVALID_ID(pigen_lvalue_id);
	}
	else if (lvalue.kind == PIGEN_LVALUE_CONCATENATION)
	{
		const pigen_lvalue_id *children = pigen_lvalue_children(model,
			lvalue.as.sequence.first_child,
			lvalue.as.sequence.child_count);
		size_t i;
		if (!children || !lvalue.as.sequence.child_count)
			return INVALID_ID(pigen_lvalue_id);
		for (i = 0; i < lvalue.as.sequence.child_count; i++)
			if (!pigen_lvalue_get(model, children[i]))
				return INVALID_ID(pigen_lvalue_id);
	}
	else
		return INVALID_ID(pigen_lvalue_id);
	expression = &model->expressions[lvalue.expression.index];
	if (expression->lvalue.index != PIGEN_INVALID_ID)
		return INVALID_ID(pigen_lvalue_id);
	if (model->lvalue_count == model->lvalue_capacity)
	{
		model->lvalue_capacity = model->lvalue_capacity ?
			model->lvalue_capacity * 2 : 16;
		model->lvalues = pigen_resize(model->lvalues,
			model->lvalue_capacity * sizeof(*model->lvalues));
	}
	result = (pigen_lvalue_id){(uint32_t)model->lvalue_count};
	model->lvalues[model->lvalue_count++] = lvalue;
	expression->lvalue = result;
	return result;
}

static size_t append_lvalue_children(pigen_semantic_model *model,
	const pigen_lvalue_id *children, size_t count)
{
	size_t first = model->lvalue_child_count;
	size_t needed;
	size_t capacity;

	if (!count || !children || count > SIZE_MAX - model->lvalue_child_count)
		return SIZE_MAX;
	needed = model->lvalue_child_count + count;
	if (needed > model->lvalue_child_capacity)
	{
		capacity = model->lvalue_child_capacity ?
			model->lvalue_child_capacity * 2 : 16;
		while (capacity < needed) capacity *= 2;
		model->lvalue_children = pigen_resize(model->lvalue_children,
			capacity * sizeof(*model->lvalue_children));
		model->lvalue_child_capacity = capacity;
	}
	memcpy(model->lvalue_children + model->lvalue_child_count,
		children, count * sizeof(*children));
	model->lvalue_child_count = needed;
	return first;
}

pigen_lvalue_id pigen_lvalue_resolve(pigen_semantic_model *model,
	pigen_expr_id expression)
{
	const pigen_semantic_expr *root;
	const pigen_semantic_expr *shape;
	const pigen_semantic_expr *base;
	const pigen_symbol *symbol;
	pigen_lvalue_id existing;
	pigen_semantic_lvalue lvalue = {0};
	pigen_signal_id signal = INVALID_ID(pigen_signal_id);

	if (!model) return INVALID_ID(pigen_lvalue_id);
	existing = find_lvalue(model, expression);
	if (existing.index != PIGEN_INVALID_ID) return existing;
	root = pigen_expr_get(model, expression);
	if (!root) return INVALID_ID(pigen_lvalue_id);
	shape = root;
	while (shape && shape->kind == PIGEN_EXPR_GROUP)
		shape = pigen_expr_get(model, shape->as.group.operand);
	if (!shape) return INVALID_ID(pigen_lvalue_id);
	lvalue.expression = expression;
	lvalue.data_type = root->data_type;
	lvalue.span = root->span;
	if (shape->kind == PIGEN_EXPR_CONCATENATION)
	{
		const pigen_expr_id *expressions = pigen_expr_children(model,
			shape->as.sequence.first_child, shape->as.sequence.child_count);
		pigen_lvalue_id *children;
		pigen_lvalue_id result;
		size_t first;
		size_t i;

		if (!expressions || !shape->as.sequence.child_count)
			return INVALID_ID(pigen_lvalue_id);
		children = pigen_resize(NULL,
			shape->as.sequence.child_count * sizeof(*children));
		for (i = 0; i < shape->as.sequence.child_count; i++)
		{
			children[i] = pigen_lvalue_resolve(model, expressions[i]);
			if (children[i].index == PIGEN_INVALID_ID)
			{
				free(children);
				return INVALID_ID(pigen_lvalue_id);
			}
		}
		first = append_lvalue_children(model, children,
			shape->as.sequence.child_count);
		free(children);
		if (first == SIZE_MAX) return INVALID_ID(pigen_lvalue_id);
		lvalue.kind = PIGEN_LVALUE_CONCATENATION;
		lvalue.as.sequence.first_child = first;
		lvalue.as.sequence.child_count = shape->as.sequence.child_count;
		result = add_lvalue(model, lvalue);
		if (result.index == PIGEN_INVALID_ID)
			model->lvalue_child_count = first;
		return result;
	}

	base = root;
	while (base && (base->kind == PIGEN_EXPR_GROUP ||
		base->kind == PIGEN_EXPR_INDEX ||
		base->kind == PIGEN_EXPR_SELECT))
	{
		pigen_expr_id next = base->kind == PIGEN_EXPR_GROUP ?
			base->as.group.operand :
			base->kind == PIGEN_EXPR_INDEX ?
				base->as.index.base : base->as.select.base;
		base = pigen_expr_get(model, next);
	}
	if (!base || base->kind != PIGEN_EXPR_SYMBOL)
		return INVALID_ID(pigen_lvalue_id);
	symbol = pigen_symbol_get(model, base->as.symbol);
	if (!symbol || symbol->kind != PIGEN_SYMBOL_SIGNAL)
		return INVALID_ID(pigen_lvalue_id);
	signal = pigen_symbol_signal(model, base->as.symbol);
	if (signal.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_lvalue_id);
	lvalue.kind = PIGEN_LVALUE_PROJECTION;
	lvalue.as.projection.base_symbol = base->as.symbol;
	lvalue.as.projection.signal = signal;
	return add_lvalue(model, lvalue);
}

const pigen_semantic_lvalue *pigen_lvalue_get(
	const pigen_semantic_model *model, pigen_lvalue_id lvalue)
{
	if (!model || lvalue.index == PIGEN_INVALID_ID ||
		lvalue.index >= model->lvalue_count)
		return NULL;
	return &model->lvalues[lvalue.index];
}

const pigen_lvalue_id *pigen_lvalue_children(
	const pigen_semantic_model *model, size_t first, size_t count)
{
	if (!model || first > model->lvalue_child_count ||
		count > model->lvalue_child_count - first)
		return NULL;
	return model->lvalue_children + first;
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
	pigen_scope_id scope, pigen_symbol_kind kind, pigen_data_type_id type,
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
		((kind == PIGEN_SYMBOL_SIGNAL || kind == PIGEN_SYMBOL_PARAMETER ||
			kind == PIGEN_SYMBOL_TYPEDEF) &&
			!pigen_data_type_exists(model, type)) ||
		((kind == PIGEN_SYMBOL_MODULE || kind == PIGEN_SYMBOL_PIPELINE ||
			kind == PIGEN_SYMBOL_STAGE || kind == PIGEN_SYMBOL_FSM ||
			kind == PIGEN_SYMBOL_FABRIC) && type.index != PIGEN_INVALID_ID) ||
		kind < PIGEN_SYMBOL_SIGNAL || kind > PIGEN_SYMBOL_FABRIC ||
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
	symbol.data_type = type;
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
		case PIGEN_SYMBOL_SIGNAL:
			symbol.object.signal = INVALID_ID(pigen_signal_id);
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
		symbol->data_type.index != expression->data_type.index ||
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

static int scope_is_within(const pigen_semantic_model *model,
	pigen_scope_id scope, pigen_scope_id ancestor)
{
	while (pigen_scope_get(model, scope))
	{
		if (scope.index == ancestor.index) return 1;
		scope = model->scopes[scope.index].parent;
	}
	return 0;
}

pigen_signal_id pigen_signal_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module_id,
	pigen_symbol_id symbol_id, pigen_data_type_id data_type,
	pigen_shape_id shape, pigen_expr_id fifo_depth,
	pigen_transfer_type transfer_type,
	pigen_semantic_direction direction, pigen_source_span span)
{
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	pigen_symbol *symbol = symbol_id.index < model->symbol_count ?
		&model->symbols[symbol_id.index] : NULL;
	pigen_signal_id result;
	int fifo = transfer_type == PIGEN_TRANSFER_TYPE_FIFO;

	if (!module || !symbol || symbol->kind != PIGEN_SYMBOL_SIGNAL ||
		symbol->object.signal.index != PIGEN_INVALID_ID ||
		!scope_is_within(model, symbol->scope, module->scope) ||
		symbol->data_type.index != data_type.index ||
		!spans_equal(symbol->declaration, span) ||
		!pigen_data_type_exists(model, data_type) ||
		!pigen_shape_get(model, shape) ||
		transfer_type < PIGEN_TRANSFER_TYPE_ABSTRACT ||
		transfer_type > PIGEN_TRANSFER_TYPE_SKID ||
		direction < PIGEN_SEMANTIC_INTERNAL ||
		direction > PIGEN_SEMANTIC_INOUT ||
		(fifo && (!pigen_expr_get(model, fifo_depth) ||
			pigen_expr_constant(model, fifo_depth).index == PIGEN_INVALID_ID)) ||
		(!fifo && fifo_depth.index != PIGEN_INVALID_ID) ||
		syntax.index == PIGEN_INVALID_ID ||
		!pigen_source_span_valid(model->sources, span) ||
		!id_capacity_available(model->signal_count))
		return INVALID_ID(pigen_signal_id);
	if (model->signal_count == model->signal_capacity)
	{
		model->signal_capacity = model->signal_capacity ?
			model->signal_capacity * 2 : 32;
		model->signals = pigen_resize(model->signals,
			model->signal_capacity * sizeof(*model->signals));
	}
	result = (pigen_signal_id){(uint32_t)model->signal_count};
	model->signals[model->signal_count++] = (pigen_semantic_signal){
		syntax, module_id, symbol_id, data_type, shape, fifo_depth, transfer_type,
		direction, INVALID_ID(pigen_clock_domain_id), span};
	symbol->object.signal = result;
	return result;
}

int pigen_signal_bind_domain(pigen_semantic_model *model,
	pigen_signal_id signal_id, pigen_clock_domain_id domain)
{
	pigen_semantic_signal *signal = signal_id.index <
		model->signal_count ? &model->signals[signal_id.index] : NULL;
	const pigen_transfer_type_laws *laws = signal ?
		pigen_transfer_type_get(signal->transfer_type) : NULL;

	if (!signal || !laws || !pigen_clock_domain_get(model, domain)) return 0;
	if (!laws->binds_domain) return 1;
	if (signal->domain.index == PIGEN_INVALID_ID)
	{
		signal->domain = domain;
		return 1;
	}
	return signal->domain.index == domain.index;
}

pigen_clock_domain_id pigen_clock_domain_intern(
	pigen_semantic_model *model, pigen_symbol_id clock_symbol,
	pigen_semantic_edge edge)
{
	const pigen_symbol *symbol = pigen_symbol_get(model, clock_symbol);
	size_t i;
	pigen_clock_domain_id result;

	if (!symbol || symbol->kind != PIGEN_SYMBOL_SIGNAL ||
		edge < PIGEN_SEMANTIC_POSEDGE || edge > PIGEN_SEMANTIC_NEGEDGE ||
		pigen_symbol_signal(model, clock_symbol).index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_clock_domain_id);
	for (i = 0; i < model->clock_domain_count; i++)
		if (model->clock_domains[i].clock_symbol.index == clock_symbol.index &&
			model->clock_domains[i].edge == edge)
			return (pigen_clock_domain_id){(uint32_t)i};
	if (!id_capacity_available(model->clock_domain_count))
		return INVALID_ID(pigen_clock_domain_id);
	if (model->clock_domain_count == model->clock_domain_capacity)
	{
		model->clock_domain_capacity = model->clock_domain_capacity ?
			model->clock_domain_capacity * 2 : 8;
		model->clock_domains = pigen_resize(model->clock_domains,
			model->clock_domain_capacity * sizeof(*model->clock_domains));
	}
	result = (pigen_clock_domain_id){(uint32_t)model->clock_domain_count};
	model->clock_domains[model->clock_domain_count++] =
		(pigen_semantic_clock_domain){clock_symbol, edge};
	return result;
}

pigen_process_id pigen_process_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module_id,
	pigen_clock_domain_id domain_id, pigen_expr_id clock,
	pigen_source_span span)
{
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	const pigen_semantic_clock_domain *domain = pigen_clock_domain_get(model,
		domain_id);
	const pigen_semantic_expr *clock_expression = pigen_expr_get(model, clock);
	pigen_process_id result;

	if (!module || !domain || !clock_expression ||
		clock_expression->kind != PIGEN_EXPR_SYMBOL ||
		clock_expression->as.symbol.index != domain->clock_symbol.index ||
		!span_contains(span, clock_expression->span) ||
		syntax.index == PIGEN_INVALID_ID ||
		!pigen_source_span_valid(model->sources, span) ||
		!id_capacity_available(model->process_count))
		return INVALID_ID(pigen_process_id);
	if (model->process_count == model->process_capacity)
	{
		model->process_capacity = model->process_capacity ?
			model->process_capacity * 2 : 16;
		model->processes = pigen_resize(model->processes,
			model->process_capacity * sizeof(*model->processes));
	}
	result = (pigen_process_id){(uint32_t)model->process_count};
	model->processes[model->process_count++] = (pigen_semantic_process){
		syntax, module_id, domain_id, clock, span};
	return result;
}

pigen_transfer_id pigen_transfer_add(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module_id,
	pigen_process_id process_id, pigen_lvalue_id destination,
	pigen_expr_id value, pigen_predicate_id guard,
	pigen_clock_domain_id domain_id,
	const pigen_transfer_signal_use *signal_uses,
	size_t signal_use_count, pigen_source_span span)
{
	const pigen_semantic_module *module = pigen_module_get(model, module_id);
	const pigen_semantic_process *process = pigen_process_get(model, process_id);
	pigen_transfer_id result;
	size_t i;
	size_t j;

	if (!module || !process || process->module.index != module_id.index ||
		process->domain.index != domain_id.index ||
		!pigen_clock_domain_get(model, domain_id) ||
		!pigen_lvalue_get(model, destination) || !pigen_expr_get(model, value) ||
		guard.index == PIGEN_INVALID_ID || guard.index >= model->predicate_count ||
		(signal_use_count && !signal_uses) ||
		!span_contains(process->span, span) || syntax.index == PIGEN_INVALID_ID ||
		!pigen_source_span_valid(model->sources, span) ||
		!id_capacity_available(model->transfer_count))
		return INVALID_ID(pigen_transfer_id);
	for (i = 0; i < signal_use_count; i++)
	{
		if (!pigen_signal_get(model, signal_uses[i].signal) ||
			!signal_uses[i].roles ||
			(signal_uses[i].roles & ~(PIGEN_TRANSFER_SIGNAL_READ |
				PIGEN_TRANSFER_SIGNAL_WRITE | PIGEN_TRANSFER_CONSUMER |
				PIGEN_TRANSFER_PRODUCER)))
			return INVALID_ID(pigen_transfer_id);
		for (j = 0; j < i; j++)
			if (signal_uses[j].signal.index ==
				signal_uses[i].signal.index)
				return INVALID_ID(pigen_transfer_id);
	}
	if (model->transfer_count == model->transfer_capacity)
	{
		model->transfer_capacity = model->transfer_capacity ?
			model->transfer_capacity * 2 : 32;
		model->transfers = pigen_resize(model->transfers,
			model->transfer_capacity * sizeof(*model->transfers));
	}
	if (model->transfer_signal_use_count + signal_use_count >
		model->transfer_signal_use_capacity)
	{
		size_t capacity = model->transfer_signal_use_capacity ?
			model->transfer_signal_use_capacity * 2 : 32;
		while (capacity < model->transfer_signal_use_count + signal_use_count)
			capacity *= 2;
		model->transfer_signal_uses = pigen_resize(
			model->transfer_signal_uses,
			capacity * sizeof(*model->transfer_signal_uses));
		model->transfer_signal_use_capacity = capacity;
	}
	result = (pigen_transfer_id){(uint32_t)model->transfer_count};
	model->transfers[model->transfer_count++] = (pigen_semantic_transfer){
		syntax, module_id, process_id, destination, value, guard, domain_id,
		model->transfer_signal_use_count, signal_use_count, span};
	if (signal_use_count)
		memcpy(model->transfer_signal_uses +
			model->transfer_signal_use_count, signal_uses,
			signal_use_count * sizeof(*signal_uses));
	model->transfer_signal_use_count += signal_use_count;
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

pigen_signal_id pigen_symbol_signal(const pigen_semantic_model *model,
	pigen_symbol_id symbol_id)
{
	const pigen_symbol *symbol = pigen_symbol_get(model, symbol_id);
	const pigen_semantic_signal *signal;
	if (!symbol || symbol->kind != PIGEN_SYMBOL_SIGNAL)
		return INVALID_ID(pigen_signal_id);
	signal = pigen_signal_get(model, symbol->object.signal);
	return signal && signal->symbol.index == symbol_id.index ?
		symbol->object.signal : INVALID_ID(pigen_signal_id);
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

const pigen_semantic_signal *pigen_signal_get(
	const pigen_semantic_model *model, pigen_signal_id signal)
{
	if (signal.index == PIGEN_INVALID_ID ||
		signal.index >= model->signal_count)
		return NULL;
	return &model->signals[signal.index];
}

const pigen_semantic_clock_domain *pigen_clock_domain_get(
	const pigen_semantic_model *model, pigen_clock_domain_id domain)
{
	if (domain.index == PIGEN_INVALID_ID ||
		domain.index >= model->clock_domain_count) return NULL;
	return &model->clock_domains[domain.index];
}

const pigen_semantic_process *pigen_process_get(
	const pigen_semantic_model *model, pigen_process_id process)
{
	if (process.index == PIGEN_INVALID_ID || process.index >= model->process_count)
		return NULL;
	return &model->processes[process.index];
}

const pigen_semantic_transfer *pigen_transfer_get(
	const pigen_semantic_model *model, pigen_transfer_id transfer)
{
	if (transfer.index == PIGEN_INVALID_ID ||
		transfer.index >= model->transfer_count) return NULL;
	return &model->transfers[transfer.index];
}

const pigen_transfer_signal_use *pigen_transfer_signal_uses(
	const pigen_semantic_model *model, pigen_transfer_id transfer_id)
{
	const pigen_semantic_transfer *transfer = pigen_transfer_get(model,
		transfer_id);
	if (!transfer || !transfer->signal_use_count ||
		transfer->first_signal_use +
		transfer->signal_use_count > model->transfer_signal_use_count)
		return NULL;
	return model->transfer_signal_uses + transfer->first_signal_use;
}

void pigen_free_semantic_model(pigen_semantic_model *model)
{
	free(model->data_types);
	free(model->data_type_dimensions);
	free(model->shapes);
	free(model->shape_dimensions);
	free(model->scopes);
	free(model->symbols);
	free(model->expressions);
	free(model->expression_children);
	free(model->constant_expressions);
	free(model->constant_expression_children);
	free(model->literal_states);
	free(model->predicates);
	free(model->predicate_atoms);
	free(model->lvalues);
	free(model->lvalue_children);
	free(model->modules);
	free(model->parameters);
	free(model->signals);
	free(model->clock_domains);
	free(model->processes);
	free(model->transfers);
	free(model->transfer_signal_uses);
	*model = (pigen_semantic_model){0};
}
