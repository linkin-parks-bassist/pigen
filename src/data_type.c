/* Canonical data-type capabilities and operation-result semantics. */
#include <stdlib.h>
#include <string.h>

#include "pigen/semantic.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static int dimensions_equal(const pigen_semantic_model *model,
	const pigen_semantic_type *type, const pigen_packed_dimension *dimensions,
	size_t count)
{
	size_t i;

	if (type->dimension_count != count) return 0;
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

pigen_type_id pigen_type_intern(pigen_semantic_model *model,
	pigen_semantic_type_kind kind, pigen_signedness signedness,
	pigen_symbol_id named_symbol, const pigen_packed_dimension *dimensions,
	size_t dimension_count)
{
	size_t i;
	pigen_semantic_type *type;
	pigen_type_id result;

	if (!model || !model->sources ||
		kind < PIGEN_TYPE_LOGIC || kind > PIGEN_TYPE_NAMED ||
		signedness < PIGEN_SIGN_IMPLICIT || signedness > PIGEN_SIGN_SIGNED ||
		(dimension_count && !dimensions) || model->type_count >= PIGEN_INVALID_ID)
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
		model->type_capacity = model->type_capacity ?
			model->type_capacity * 2 : 16;
		model->types = pigen_resize(model->types,
			model->type_capacity * sizeof(*model->types));
	}
	if (model->dimension_count + dimension_count > model->dimension_capacity)
	{
		size_t capacity = model->dimension_capacity ?
			model->dimension_capacity * 2 : 16;
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
	if (!model || type.index == PIGEN_INVALID_ID ||
		type.index >= model->type_count) return NULL;
	return &model->types[type.index];
}

const pigen_packed_dimension *pigen_type_dimensions(
	const pigen_semantic_model *model, pigen_type_id type)
{
	const pigen_semantic_type *known = pigen_type_get(model, type);
	if (!known || !known->dimension_count) return NULL;
	return model->dimensions + known->first_dimension;
}

static pigen_type_id packed_projection_base(
	const pigen_semantic_model *model, pigen_type_id type)
{
	size_t remaining;

	if (!model) return INVALID_ID(pigen_type_id);
	remaining = model->type_count + 1;
	while (remaining--)
	{
		const pigen_semantic_type *known = pigen_type_get(model, type);
		const pigen_symbol *symbol;
		if (!known) return INVALID_ID(pigen_type_id);
		if (known->dimension_count || known->kind != PIGEN_TYPE_NAMED)
			return type;
		symbol = pigen_symbol_get(model, known->named_symbol);
		if (!symbol || symbol->kind != PIGEN_SYMBOL_TYPEDEF)
			return INVALID_ID(pigen_type_id);
		type = symbol->type;
	}
	return INVALID_ID(pigen_type_id);
}

pigen_type_id pigen_type_packed_element(pigen_semantic_model *model,
	pigen_type_id type)
{
	const pigen_semantic_type *known;
	pigen_packed_dimension *remaining = NULL;
	pigen_semantic_type_kind kind;
	pigen_signedness signedness;
	pigen_symbol_id named_symbol;
	size_t remaining_count;
	pigen_type_id result;

	if (!model) return INVALID_ID(pigen_type_id);
	type = packed_projection_base(model, type);
	known = pigen_type_get(model, type);
	if (!known) return INVALID_ID(pigen_type_id);
	if (!known->dimension_count)
	{
		if (known->kind == PIGEN_TYPE_INTEGER)
			return pigen_semantic_boolean_result_type(model);
		return INVALID_ID(pigen_type_id);
	}
	kind = known->kind;
	signedness = known->kind == PIGEN_TYPE_NAMED ?
		known->signedness : PIGEN_SIGN_UNSIGNED;
	named_symbol = known->named_symbol;
	remaining_count = known->dimension_count - 1;
	if (remaining_count)
	{
		const pigen_packed_dimension *dimensions =
			pigen_type_dimensions(model, type);
		remaining = pigen_resize(NULL,
			remaining_count * sizeof(*remaining));
		memcpy(remaining, dimensions + 1,
			remaining_count * sizeof(*remaining));
	}
	result = pigen_type_intern(model, kind, signedness, named_symbol,
		remaining, remaining_count);
	free(remaining);
	return result;
}

int pigen_select_kind_is_valid(pigen_select_kind kind)
{
	return kind >= PIGEN_SEMANTIC_SELECT_RANGE &&
		kind <= PIGEN_SEMANTIC_SELECT_INDEXED_DOWN;
}

static pigen_type_id packed_select_with_dimension(
	pigen_semantic_model *model, pigen_type_id type,
	pigen_packed_dimension selected)
{
	const pigen_semantic_type *known;
	pigen_packed_dimension *dimensions;
	pigen_semantic_type_kind kind;
	pigen_symbol_id named_symbol;
	size_t dimension_count;
	pigen_type_id result;

	type = packed_projection_base(model, type);
	known = pigen_type_get(model, type);
	if (!known) return INVALID_ID(pigen_type_id);
	if (!known->dimension_count)
	{
		if (known->kind != PIGEN_TYPE_INTEGER)
			return INVALID_ID(pigen_type_id);
		kind = PIGEN_TYPE_LOGIC;
		named_symbol = INVALID_ID(pigen_symbol_id);
		dimension_count = 1;
		dimensions = pigen_resize(NULL, sizeof(*dimensions));
		dimensions[0] = selected;
	}
	else
	{
		const pigen_packed_dimension *base_dimensions =
			pigen_type_dimensions(model, type);
		kind = known->kind;
		named_symbol = known->named_symbol;
		dimension_count = known->dimension_count;
		dimensions = pigen_resize(NULL,
			dimension_count * sizeof(*dimensions));
		dimensions[0] = selected;
		if (dimension_count > 1)
			memcpy(dimensions + 1, base_dimensions + 1,
				(dimension_count - 1) * sizeof(*dimensions));
	}
	result = pigen_type_intern(model, kind, PIGEN_SIGN_UNSIGNED,
		named_symbol, dimensions, dimension_count);
	free(dimensions);
	return result;
}

pigen_type_id pigen_type_packed_select(pigen_semantic_model *model,
	pigen_type_id type, pigen_const_expr_id left,
	pigen_const_expr_id right, pigen_select_kind kind)
{
	pigen_type_id integer_type;
	pigen_const_expr_id width;
	pigen_const_expr_id one;
	pigen_const_expr_id zero;
	pigen_const_expr_id upper;
	pigen_packed_dimension dimension;

	if (!model || !pigen_select_kind_is_valid(kind) ||
		!pigen_const_expr_get(model, right) ||
		(kind == PIGEN_SEMANTIC_SELECT_RANGE) !=
			(left.index != PIGEN_INVALID_ID))
		return INVALID_ID(pigen_type_id);
	integer_type = pigen_semantic_integer_type(model);
	width = pigen_const_expr_intern_select_width(model, left, right, kind);
	one = pigen_const_expr_intern_integer(model, 1, integer_type);
	zero = pigen_const_expr_intern_integer(model, 0, integer_type);
	upper = pigen_const_expr_intern_binary(model, PIGEN_BINARY_SUBTRACT,
		width, one, integer_type);
	if (width.index == PIGEN_INVALID_ID ||
		one.index == PIGEN_INVALID_ID || zero.index == PIGEN_INVALID_ID ||
		upper.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_type_id);
	dimension = (pigen_packed_dimension){upper, zero};
	return packed_select_with_dimension(model, type, dimension);
}

static pigen_type_id underlying_type(const pigen_semantic_model *model,
	pigen_type_id type)
{
	size_t remaining;

	if (!model) return INVALID_ID(pigen_type_id);
	remaining = model->type_count + 1;
	while (remaining--)
	{
		const pigen_semantic_type *known = pigen_type_get(model, type);
		const pigen_symbol *symbol;

		if (!known) return INVALID_ID(pigen_type_id);
		if (known->kind != PIGEN_TYPE_NAMED) return type;
		symbol = pigen_symbol_get(model, known->named_symbol);
		if (!symbol || symbol->kind != PIGEN_SYMBOL_TYPEDEF)
			return INVALID_ID(pigen_type_id);
		type = symbol->type;
	}
	return INVALID_ID(pigen_type_id);
}

int pigen_data_type_is_integral(const pigen_semantic_model *model,
	pigen_type_id type)
{
	const pigen_semantic_type *known = pigen_type_get(model,
		underlying_type(model, type));

	return known && (known->kind == PIGEN_TYPE_INTEGER ||
		known->kind == PIGEN_TYPE_LOGIC || known->kind == PIGEN_TYPE_BIT);
}

pigen_state_domain pigen_data_type_state_domain(
	const pigen_semantic_model *model, pigen_type_id type)
{
	const pigen_semantic_type *known = pigen_type_get(model,
		underlying_type(model, type));

	if (!known) return PIGEN_DATA_TYPE_STATE_INVALID;
	if (known->kind == PIGEN_TYPE_BIT) return PIGEN_DATA_TYPE_STATE_TWO;
	if (known->kind == PIGEN_TYPE_LOGIC || known->kind == PIGEN_TYPE_INTEGER)
		return PIGEN_DATA_TYPE_STATE_FOUR;
	return PIGEN_DATA_TYPE_STATE_INVALID;
}

pigen_type_id pigen_data_type_sized_logic(pigen_semantic_model *model,
	size_t width, pigen_signedness signedness)
{
	pigen_packed_dimension dimension;
	pigen_const_expr_id left;
	pigen_const_expr_id right;
	pigen_type_id integer_type;

	if (!model || !width || (signedness != PIGEN_SIGN_SIGNED &&
		signedness != PIGEN_SIGN_UNSIGNED))
		return INVALID_ID(pigen_type_id);
	if (width == 1)
		return pigen_type_intern(model, PIGEN_TYPE_LOGIC, signedness,
			INVALID_ID(pigen_symbol_id), NULL, 0);
	integer_type = pigen_semantic_integer_type(model);
	left = pigen_const_expr_intern_integer(model, (uint64_t)(width - 1),
		integer_type);
	right = pigen_const_expr_intern_integer(model, 0, integer_type);
	if (left.index == PIGEN_INVALID_ID || right.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_type_id);
	dimension = (pigen_packed_dimension){left, right};
	return pigen_type_intern(model, PIGEN_TYPE_LOGIC, signedness,
		INVALID_ID(pigen_symbol_id), &dimension, 1);
}

static int unary_boolean_result(pigen_unary_operator operator)
{
	return operator == PIGEN_UNARY_LOGICAL_NOT ||
		(operator >= PIGEN_UNARY_REDUCTION_AND &&
		operator <= PIGEN_UNARY_REDUCTION_XNOR);
}

pigen_type_id pigen_data_type_unary_result(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_type_id operand)
{
	if (!pigen_data_type_is_integral(model, operand) ||
		operator < PIGEN_UNARY_POSITIVE ||
		operator > PIGEN_UNARY_REDUCTION_XNOR)
		return INVALID_ID(pigen_type_id);
	return unary_boolean_result(operator) ?
		pigen_semantic_boolean_result_type(model) : operand;
}

static int binary_boolean_result(pigen_binary_operator operator)
{
	return (operator >= PIGEN_BINARY_LESS &&
		operator <= PIGEN_BINARY_WILDCARD_NOT_EQUAL) ||
		operator == PIGEN_BINARY_LOGICAL_AND ||
		operator == PIGEN_BINARY_LOGICAL_OR;
}

pigen_type_id pigen_data_type_binary_result(pigen_semantic_model *model,
	pigen_binary_operator operator, pigen_type_id left, pigen_type_id right)
{
	if (!pigen_data_type_is_integral(model, left) ||
		!pigen_data_type_is_integral(model, right) ||
		operator < PIGEN_BINARY_ADD || operator > PIGEN_BINARY_LOGICAL_OR)
		return INVALID_ID(pigen_type_id);
	if (binary_boolean_result(operator))
		return pigen_semantic_boolean_result_type(model);
	return left.index == right.index ? left : INVALID_ID(pigen_type_id);
}

pigen_type_id pigen_data_type_conditional_result(pigen_semantic_model *model,
	pigen_type_id condition, pigen_type_id when_true,
	pigen_type_id when_false)
{
	if (!pigen_data_type_is_integral(model, condition) ||
		when_true.index != when_false.index ||
		!pigen_type_get(model, when_true))
		return INVALID_ID(pigen_type_id);
	return when_true;
}

static pigen_const_expr_id packed_width(
	pigen_semantic_model *model, pigen_type_id type, size_t remaining)
{
	const pigen_semantic_type *known = pigen_type_get(model, type);
	const pigen_packed_dimension *dimensions;
	pigen_const_expr_id *factors;
	pigen_const_expr_id result;
	size_t factor_count = 0;
	size_t i;

	if (!known || !remaining) return INVALID_ID(pigen_const_expr_id);
	factors = pigen_resize(NULL,
		(known->dimension_count + 1) * sizeof(*factors));
	dimensions = pigen_type_dimensions(model, type);
	for (i = 0; i < known->dimension_count; i++)
	{
		factors[factor_count] = pigen_const_expr_intern_select_width(model,
			dimensions[i].left, dimensions[i].right,
			PIGEN_SEMANTIC_SELECT_RANGE);
		if (factors[factor_count].index == PIGEN_INVALID_ID)
		{
			free(factors);
			return INVALID_ID(pigen_const_expr_id);
		}
		factor_count++;
	}
	if (known->kind == PIGEN_TYPE_NAMED)
	{
		const pigen_symbol *symbol =
			pigen_symbol_get(model, known->named_symbol);
		if (!symbol || symbol->kind != PIGEN_SYMBOL_TYPEDEF)
		{
			free(factors);
			return INVALID_ID(pigen_const_expr_id);
		}
		factors[factor_count] = packed_width(model, symbol->type,
			remaining - 1);
	}
	else
		factors[factor_count] = pigen_const_expr_intern_integer(model,
			known->kind == PIGEN_TYPE_INTEGER ? 32 : 1,
			pigen_semantic_integer_type(model));
	if (factors[factor_count].index == PIGEN_INVALID_ID)
	{
		free(factors);
		return INVALID_ID(pigen_const_expr_id);
	}
	factor_count++;
	result = pigen_const_expr_intern_width_product(model, factors,
		factor_count);
	free(factors);
	return result;
}

pigen_const_expr_id pigen_type_packed_width(pigen_semantic_model *model,
	pigen_type_id type)
{
	return model ? packed_width(model, type, model->type_count + 1) :
		INVALID_ID(pigen_const_expr_id);
}

pigen_type_id pigen_type_concatenation(pigen_semantic_model *model,
	const pigen_type_id *types, size_t count)
{
	pigen_const_expr_id *widths;
	pigen_const_expr_id width;
	pigen_const_expr_id one;
	pigen_const_expr_id zero;
	pigen_const_expr_id upper;
	pigen_type_id integer_type;
	pigen_semantic_type_kind kind = PIGEN_TYPE_BIT;
	pigen_packed_dimension dimension;
	const pigen_const_expr *known_width;
	pigen_type_id result;
	size_t i;

	if (!model || !types || !count) return INVALID_ID(pigen_type_id);
	widths = pigen_resize(NULL, count * sizeof(*widths));
	for (i = 0; i < count; i++)
	{
		pigen_state_domain state = pigen_data_type_state_domain(model,
			types[i]);
		if (state == PIGEN_DATA_TYPE_STATE_INVALID)
		{
			free(widths);
			return INVALID_ID(pigen_type_id);
		}
		if (state == PIGEN_DATA_TYPE_STATE_FOUR) kind = PIGEN_TYPE_LOGIC;
		widths[i] = pigen_type_packed_width(model, types[i]);
		if (widths[i].index == PIGEN_INVALID_ID)
		{
			free(widths);
			return INVALID_ID(pigen_type_id);
		}
	}
	width = pigen_const_expr_intern_width_sum(model, widths, count);
	free(widths);
	known_width = pigen_const_expr_get(model, width);
	if (!known_width) return INVALID_ID(pigen_type_id);
	if (known_width->kind == PIGEN_CONST_EXPR_INTEGER &&
		known_width->as.integer == 1)
		return pigen_type_intern(model, kind, PIGEN_SIGN_UNSIGNED,
			INVALID_ID(pigen_symbol_id), NULL, 0);
	integer_type = pigen_semantic_integer_type(model);
	one = pigen_const_expr_intern_integer(model, 1, integer_type);
	zero = pigen_const_expr_intern_integer(model, 0, integer_type);
	upper = pigen_const_expr_intern_binary(model, PIGEN_BINARY_SUBTRACT,
		width, one, integer_type);
	if (upper.index == PIGEN_INVALID_ID || zero.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_type_id);
	dimension = (pigen_packed_dimension){upper, zero};
	result = pigen_type_intern(model, kind, PIGEN_SIGN_UNSIGNED,
		INVALID_ID(pigen_symbol_id), &dimension, 1);
	return result;
}
