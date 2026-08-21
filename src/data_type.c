/* Canonical data-type capabilities and operation-result semantics. */
#include <stdlib.h>
#include <string.h>

#include "pigen/semantic.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef enum {
	PIGEN_DATA_TYPE_LOGIC,
	PIGEN_DATA_TYPE_BIT,
	PIGEN_DATA_TYPE_INTEGER,
	PIGEN_DATA_TYPE_ALIAS
} data_type_constructor;

struct pigen_data_type {
	data_type_constructor constructor;
	pigen_signedness signedness;
	pigen_symbol_id alias;
	pigen_data_type_id alias_target;
	size_t first_dimension;
	size_t dimension_count;
};

static int spelling_is(const pigen_semantic_model *model,
	pigen_source_span spelling, const char *expected)
{
	size_t length;
	const char *text = pigen_source_span_text(model->sources, spelling, &length);
	size_t expected_length = strlen(expected);

	return text && length == expected_length && !memcmp(text, expected, length);
}

static int dimensions_equal(const pigen_semantic_model *model,
	const pigen_data_type *data_type,
	const pigen_packed_dimension *dimensions,
	size_t count)
{
	size_t i;

	if (data_type->dimension_count != count) return 0;
	for (i = 0; i < count; i++)
	{
		const pigen_packed_dimension *known =
			&model->data_type_dimensions[data_type->first_dimension + i];
		if (known->left.index != dimensions[i].left.index ||
			known->right.index != dimensions[i].right.index)
			return 0;
	}
	return 1;
}

static pigen_data_type_id data_type_intern(pigen_semantic_model *model,
	data_type_constructor constructor, pigen_signedness signedness,
	pigen_symbol_id alias, pigen_data_type_id alias_target,
	const pigen_packed_dimension *dimensions,
	size_t dimension_count)
{
	size_t i;
	pigen_data_type *data_type;
	pigen_data_type_id result;

	if (!model || !model->sources ||
		constructor < PIGEN_DATA_TYPE_LOGIC ||
		constructor > PIGEN_DATA_TYPE_ALIAS ||
		signedness < PIGEN_SIGN_IMPLICIT || signedness > PIGEN_SIGN_SIGNED ||
		(dimension_count && !dimensions) ||
		model->data_type_count >= PIGEN_INVALID_ID)
		return INVALID_ID(pigen_data_type_id);
	if (constructor == PIGEN_DATA_TYPE_ALIAS)
	{
		if (alias.index == PIGEN_INVALID_ID ||
			alias_target.index == PIGEN_INVALID_ID ||
			alias_target.index >= model->data_type_count)
			return INVALID_ID(pigen_data_type_id);
	}
	else if (alias.index != PIGEN_INVALID_ID ||
		alias_target.index != PIGEN_INVALID_ID)
		return INVALID_ID(pigen_data_type_id);
	for (i = 0; i < dimension_count; i++)
		if (!pigen_const_expr_get(model, dimensions[i].left) ||
			!pigen_const_expr_get(model, dimensions[i].right))
			return INVALID_ID(pigen_data_type_id);
	for (i = 0; i < model->data_type_count; i++)
	{
		data_type = &model->data_types[i];
		if (data_type->constructor == constructor &&
			data_type->signedness == signedness &&
			data_type->alias.index == alias.index &&
			data_type->alias_target.index == alias_target.index &&
			dimensions_equal(model, data_type, dimensions, dimension_count))
			return (pigen_data_type_id){(uint32_t)i};
	}
	if (model->data_type_count == model->data_type_capacity)
	{
		model->data_type_capacity = model->data_type_capacity ?
			model->data_type_capacity * 2 : 16;
		model->data_types = pigen_resize(model->data_types,
			model->data_type_capacity * sizeof(*model->data_types));
	}
	if (model->data_type_dimension_count + dimension_count >
		model->data_type_dimension_capacity)
	{
		size_t capacity = model->data_type_dimension_capacity ?
			model->data_type_dimension_capacity * 2 : 16;
		while (capacity < model->data_type_dimension_count + dimension_count)
			capacity *= 2;
		model->data_type_dimensions = pigen_resize(model->data_type_dimensions,
			capacity * sizeof(*model->data_type_dimensions));
		model->data_type_dimension_capacity = capacity;
	}
	result = (pigen_data_type_id){(uint32_t)model->data_type_count};
	data_type = &model->data_types[model->data_type_count++];
	*data_type = (pigen_data_type){constructor, signedness, alias, alias_target,
		model->data_type_dimension_count, dimension_count};
	if (dimension_count)
		memcpy(model->data_type_dimensions + model->data_type_dimension_count,
			dimensions,
			dimension_count * sizeof(*dimensions));
	model->data_type_dimension_count += dimension_count;
	return result;
}

pigen_data_type_id pigen_data_type_primitive_from_spelling(
	pigen_semantic_model *model, pigen_source_span spelling,
	pigen_signedness signedness, const pigen_packed_dimension *dimensions,
	size_t dimension_count)
{
	data_type_constructor constructor;

	if (!model || !model->sources) return INVALID_ID(pigen_data_type_id);
	if (spelling_is(model, spelling, "logic")) constructor = PIGEN_DATA_TYPE_LOGIC;
	else if (spelling_is(model, spelling, "bit")) constructor = PIGEN_DATA_TYPE_BIT;
	else return INVALID_ID(pigen_data_type_id);
	return data_type_intern(model, constructor, signedness,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
		dimensions, dimension_count);
}

pigen_data_type_id pigen_data_type_implicit(pigen_semantic_model *model,
	pigen_signedness signedness, const pigen_packed_dimension *dimensions,
	size_t dimension_count)
{
	return data_type_intern(model, PIGEN_DATA_TYPE_LOGIC, signedness,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
		dimensions, dimension_count);
}

pigen_data_type_id pigen_data_type_alias(pigen_semantic_model *model,
	pigen_symbol_id alias, pigen_data_type_id target,
	pigen_signedness signedness,
	const pigen_packed_dimension *dimensions, size_t dimension_count)
{
	return data_type_intern(model, PIGEN_DATA_TYPE_ALIAS, signedness, alias,
		target, dimensions, dimension_count);
}

pigen_data_type_id pigen_data_type_integer(pigen_semantic_model *model)
{
	if (model->integer_data_type.index == PIGEN_INVALID_ID)
		model->integer_data_type = data_type_intern(model,
			PIGEN_DATA_TYPE_INTEGER,
			PIGEN_SIGN_SIGNED, INVALID_ID(pigen_symbol_id),
			INVALID_ID(pigen_data_type_id), NULL, 0);
	return model->integer_data_type;
}

pigen_data_type_id pigen_data_type_boolean(pigen_semantic_model *model)
{
	if (model->boolean_data_type.index == PIGEN_INVALID_ID)
		model->boolean_data_type = data_type_intern(model,
			PIGEN_DATA_TYPE_LOGIC,
			PIGEN_SIGN_UNSIGNED, INVALID_ID(pigen_symbol_id),
			INVALID_ID(pigen_data_type_id), NULL, 0);
	return model->boolean_data_type;
}

static const pigen_data_type *data_type_get(const pigen_semantic_model *model,
	pigen_data_type_id data_type)
{
	if (!model || data_type.index == PIGEN_INVALID_ID ||
		data_type.index >= model->data_type_count) return NULL;
	return &model->data_types[data_type.index];
}

int pigen_data_type_exists(const pigen_semantic_model *model,
	pigen_data_type_id data_type)
{
	return data_type_get(model, data_type) != NULL;
}

pigen_signedness pigen_data_type_signedness(
	const pigen_semantic_model *model, pigen_data_type_id data_type)
{
	const pigen_data_type *known = data_type_get(model, data_type);
	return known ? known->signedness : PIGEN_SIGN_INVALID;
}

size_t pigen_data_type_dimension_count(const pigen_semantic_model *model,
	pigen_data_type_id data_type)
{
	const pigen_data_type *known = data_type_get(model, data_type);
	return known ? known->dimension_count : 0;
}

pigen_symbol_id pigen_data_type_alias_symbol(
	const pigen_semantic_model *model, pigen_data_type_id data_type)
{
	const pigen_data_type *known = data_type_get(model, data_type);
	return known && known->constructor == PIGEN_DATA_TYPE_ALIAS ? known->alias :
		INVALID_ID(pigen_symbol_id);
}

pigen_data_type_id pigen_data_type_alias_target(
	const pigen_semantic_model *model, pigen_data_type_id data_type)
{
	const pigen_data_type *known = data_type_get(model, data_type);
	return known && known->constructor == PIGEN_DATA_TYPE_ALIAS ?
		known->alias_target : INVALID_ID(pigen_data_type_id);
}

const pigen_packed_dimension *pigen_data_type_dimensions(
	const pigen_semantic_model *model, pigen_data_type_id type)
{
	const pigen_data_type *known = data_type_get(model, type);
	if (!known || !known->dimension_count) return NULL;
	return model->data_type_dimensions + known->first_dimension;
}

static pigen_data_type_id packed_projection_base(
	const pigen_semantic_model *model, pigen_data_type_id type)
{
	size_t remaining;

	if (!model) return INVALID_ID(pigen_data_type_id);
	remaining = model->data_type_count + 1;
	while (remaining--)
	{
		const pigen_data_type *known = data_type_get(model, type);
		if (!known) return INVALID_ID(pigen_data_type_id);
		if (known->dimension_count || known->constructor != PIGEN_DATA_TYPE_ALIAS)
			return type;
		type = known->alias_target;
	}
	return INVALID_ID(pigen_data_type_id);
}

pigen_data_type_id pigen_data_type_packed_element(pigen_semantic_model *model,
	pigen_data_type_id type)
{
	const pigen_data_type *known;
	pigen_packed_dimension *remaining = NULL;
	data_type_constructor constructor;
	pigen_signedness signedness;
	pigen_symbol_id alias;
	size_t remaining_count;
	pigen_data_type_id result;

	if (!model) return INVALID_ID(pigen_data_type_id);
	type = packed_projection_base(model, type);
	known = data_type_get(model, type);
	if (!known) return INVALID_ID(pigen_data_type_id);
	if (!known->dimension_count)
	{
		if (known->constructor == PIGEN_DATA_TYPE_INTEGER)
			return pigen_data_type_boolean(model);
		return INVALID_ID(pigen_data_type_id);
	}
	constructor = known->constructor;
	signedness = known->constructor == PIGEN_DATA_TYPE_ALIAS ?
		known->signedness : PIGEN_SIGN_UNSIGNED;
	alias = known->alias;
	remaining_count = known->dimension_count - 1;
	if (remaining_count)
	{
		const pigen_packed_dimension *dimensions =
			pigen_data_type_dimensions(model, type);
		remaining = pigen_resize(NULL,
			remaining_count * sizeof(*remaining));
		memcpy(remaining, dimensions + 1,
			remaining_count * sizeof(*remaining));
	}
	result = data_type_intern(model, constructor, signedness, alias,
		known->alias_target, remaining, remaining_count);
	free(remaining);
	return result;
}

static pigen_data_type_id packed_select_with_dimension(
	pigen_semantic_model *model, pigen_data_type_id type,
	pigen_packed_dimension selected)
{
	const pigen_data_type *known;
	pigen_packed_dimension *dimensions;
	data_type_constructor constructor;
	pigen_symbol_id alias;
	size_t dimension_count;
	pigen_data_type_id result;

	type = packed_projection_base(model, type);
	known = data_type_get(model, type);
	if (!known) return INVALID_ID(pigen_data_type_id);
	if (!known->dimension_count)
	{
		if (known->constructor != PIGEN_DATA_TYPE_INTEGER)
			return INVALID_ID(pigen_data_type_id);
		constructor = PIGEN_DATA_TYPE_LOGIC;
		alias = INVALID_ID(pigen_symbol_id);
		dimension_count = 1;
		dimensions = pigen_resize(NULL, sizeof(*dimensions));
		dimensions[0] = selected;
	}
	else
	{
		const pigen_packed_dimension *base_dimensions =
			pigen_data_type_dimensions(model, type);
		constructor = known->constructor;
		alias = known->alias;
		dimension_count = known->dimension_count;
		dimensions = pigen_resize(NULL,
			dimension_count * sizeof(*dimensions));
		dimensions[0] = selected;
		if (dimension_count > 1)
			memcpy(dimensions + 1, base_dimensions + 1,
				(dimension_count - 1) * sizeof(*dimensions));
	}
	result = data_type_intern(model, constructor, PIGEN_SIGN_UNSIGNED,
		alias, known->alias_target, dimensions, dimension_count);
	free(dimensions);
	return result;
}

pigen_data_type_id pigen_data_type_packed_select(pigen_semantic_model *model,
	pigen_data_type_id type, pigen_const_expr_id left,
	pigen_const_expr_id right, pigen_select_kind kind)
{
	pigen_data_type_id integer_type;
	pigen_const_expr_id width;
	pigen_const_expr_id one;
	pigen_const_expr_id zero;
	pigen_const_expr_id upper;
	pigen_packed_dimension dimension;

	if (!model || !pigen_select_kind_is_valid(kind) ||
		!pigen_const_expr_get(model, right) ||
		(kind == PIGEN_SEMANTIC_SELECT_RANGE) !=
			(left.index != PIGEN_INVALID_ID))
		return INVALID_ID(pigen_data_type_id);
	integer_type = pigen_data_type_integer(model);
	width = pigen_const_expr_intern_select_width(model, left, right, kind);
	one = pigen_const_expr_intern_integer(model, 1, integer_type);
	zero = pigen_const_expr_intern_integer(model, 0, integer_type);
	upper = pigen_const_expr_intern_binary(model, PIGEN_BINARY_SUBTRACT,
		width, one, integer_type);
	if (width.index == PIGEN_INVALID_ID ||
		one.index == PIGEN_INVALID_ID || zero.index == PIGEN_INVALID_ID ||
		upper.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_data_type_id);
	dimension = (pigen_packed_dimension){upper, zero};
	return packed_select_with_dimension(model, type, dimension);
}

static pigen_data_type_id underlying_data_type(
	const pigen_semantic_model *model,
	pigen_data_type_id type)
{
	size_t remaining;

	if (!model) return INVALID_ID(pigen_data_type_id);
	remaining = model->data_type_count + 1;
	while (remaining--)
	{
		const pigen_data_type *known = data_type_get(model, type);

		if (!known) return INVALID_ID(pigen_data_type_id);
		if (known->constructor != PIGEN_DATA_TYPE_ALIAS) return type;
		type = known->alias_target;
	}
	return INVALID_ID(pigen_data_type_id);
}

int pigen_data_type_is_integral(const pigen_semantic_model *model,
	pigen_data_type_id type)
{
	const pigen_data_type *known = data_type_get(model,
		underlying_data_type(model, type));

	return known && (known->constructor == PIGEN_DATA_TYPE_INTEGER ||
		known->constructor == PIGEN_DATA_TYPE_LOGIC || known->constructor == PIGEN_DATA_TYPE_BIT);
}

pigen_state_domain pigen_data_type_state_domain(
	const pigen_semantic_model *model, pigen_data_type_id type)
{
	const pigen_data_type *known = data_type_get(model,
		underlying_data_type(model, type));

	if (!known) return PIGEN_DATA_TYPE_STATE_INVALID;
	if (known->constructor == PIGEN_DATA_TYPE_BIT) return PIGEN_DATA_TYPE_STATE_TWO;
	if (known->constructor == PIGEN_DATA_TYPE_LOGIC || known->constructor == PIGEN_DATA_TYPE_INTEGER)
		return PIGEN_DATA_TYPE_STATE_FOUR;
	return PIGEN_DATA_TYPE_STATE_INVALID;
}

pigen_data_type_id pigen_data_type_sized_logic(pigen_semantic_model *model,
	size_t width, pigen_signedness signedness)
{
	pigen_packed_dimension dimension;
	pigen_const_expr_id left;
	pigen_const_expr_id right;
	pigen_data_type_id integer_type;

	if (!model || !width || (signedness != PIGEN_SIGN_SIGNED &&
		signedness != PIGEN_SIGN_UNSIGNED))
		return INVALID_ID(pigen_data_type_id);
	if (width == 1)
		return data_type_intern(model, PIGEN_DATA_TYPE_LOGIC, signedness,
			INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
			NULL, 0);
	integer_type = pigen_data_type_integer(model);
	left = pigen_const_expr_intern_integer(model, (uint64_t)(width - 1),
		integer_type);
	right = pigen_const_expr_intern_integer(model, 0, integer_type);
	if (left.index == PIGEN_INVALID_ID || right.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_data_type_id);
	dimension = (pigen_packed_dimension){left, right};
	return data_type_intern(model, PIGEN_DATA_TYPE_LOGIC, signedness,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
		&dimension, 1);
}

static int unary_boolean_result(pigen_unary_operator operator)
{
	return operator == PIGEN_UNARY_LOGICAL_NOT ||
		(operator >= PIGEN_UNARY_REDUCTION_AND &&
		operator <= PIGEN_UNARY_REDUCTION_XNOR);
}

pigen_data_type_id pigen_data_type_unary_result(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_data_type_id operand)
{
	if (!pigen_data_type_is_integral(model, operand) ||
		operator < PIGEN_UNARY_POSITIVE ||
		operator > PIGEN_UNARY_REDUCTION_XNOR)
		return INVALID_ID(pigen_data_type_id);
	return unary_boolean_result(operator) ?
		pigen_data_type_boolean(model) : operand;
}

static int binary_boolean_result(pigen_binary_operator operator)
{
	return (operator >= PIGEN_BINARY_LESS &&
		operator <= PIGEN_BINARY_WILDCARD_NOT_EQUAL) ||
		operator == PIGEN_BINARY_LOGICAL_AND ||
		operator == PIGEN_BINARY_LOGICAL_OR;
}

pigen_data_type_id pigen_data_type_binary_result(pigen_semantic_model *model,
	pigen_binary_operator operator, pigen_data_type_id left, pigen_data_type_id right)
{
	if (!pigen_data_type_is_integral(model, left) ||
		!pigen_data_type_is_integral(model, right) ||
		operator < PIGEN_BINARY_ADD || operator > PIGEN_BINARY_LOGICAL_OR)
		return INVALID_ID(pigen_data_type_id);
	if (binary_boolean_result(operator))
		return pigen_data_type_boolean(model);
	return left.index == right.index ? left : INVALID_ID(pigen_data_type_id);
}

pigen_data_type_id pigen_data_type_conditional_result(pigen_semantic_model *model,
	pigen_data_type_id condition, pigen_data_type_id when_true,
	pigen_data_type_id when_false)
{
	if (!pigen_data_type_is_integral(model, condition) ||
		when_true.index != when_false.index ||
		!data_type_get(model, when_true))
		return INVALID_ID(pigen_data_type_id);
	return when_true;
}

static pigen_const_expr_id packed_width(
	pigen_semantic_model *model, pigen_data_type_id type, size_t remaining)
{
	const pigen_data_type *known = data_type_get(model, type);
	const pigen_packed_dimension *dimensions;
	pigen_const_expr_id *factors;
	pigen_const_expr_id result;
	size_t factor_count = 0;
	size_t i;

	if (!known || !remaining) return INVALID_ID(pigen_const_expr_id);
	factors = pigen_resize(NULL,
		(known->dimension_count + 1) * sizeof(*factors));
	dimensions = pigen_data_type_dimensions(model, type);
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
	if (known->constructor == PIGEN_DATA_TYPE_ALIAS)
		factors[factor_count] = packed_width(model, known->alias_target,
			remaining - 1);
	else
		factors[factor_count] = pigen_const_expr_intern_integer(model,
			known->constructor == PIGEN_DATA_TYPE_INTEGER ? 32 : 1,
			pigen_data_type_integer(model));
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

pigen_const_expr_id pigen_data_type_packed_width(pigen_semantic_model *model,
	pigen_data_type_id type)
{
	return model ? packed_width(model, type, model->data_type_count + 1) :
		INVALID_ID(pigen_const_expr_id);
}

pigen_data_type_id pigen_data_type_concatenation(pigen_semantic_model *model,
	const pigen_data_type_id *data_types, size_t count)
{
	pigen_const_expr_id *widths;
	pigen_const_expr_id width;
	pigen_const_expr_id one;
	pigen_const_expr_id zero;
	pigen_const_expr_id upper;
	pigen_data_type_id integer_type;
	data_type_constructor constructor = PIGEN_DATA_TYPE_BIT;
	pigen_packed_dimension dimension;
	const pigen_const_expr *known_width;
	pigen_data_type_id result;
	size_t i;

	if (!model || !data_types || !count)
		return INVALID_ID(pigen_data_type_id);
	widths = pigen_resize(NULL, count * sizeof(*widths));
	for (i = 0; i < count; i++)
	{
		pigen_state_domain state = pigen_data_type_state_domain(model,
			data_types[i]);
		if (state == PIGEN_DATA_TYPE_STATE_INVALID)
		{
			free(widths);
			return INVALID_ID(pigen_data_type_id);
		}
		if (state == PIGEN_DATA_TYPE_STATE_FOUR)
			constructor = PIGEN_DATA_TYPE_LOGIC;
		widths[i] = pigen_data_type_packed_width(model, data_types[i]);
		if (widths[i].index == PIGEN_INVALID_ID)
		{
			free(widths);
			return INVALID_ID(pigen_data_type_id);
		}
	}
	width = pigen_const_expr_intern_width_sum(model, widths, count);
	free(widths);
	known_width = pigen_const_expr_get(model, width);
	if (!known_width) return INVALID_ID(pigen_data_type_id);
	if (known_width->kind == PIGEN_CONST_EXPR_INTEGER &&
		known_width->as.integer == 1)
		return data_type_intern(model, constructor, PIGEN_SIGN_UNSIGNED,
			INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
			NULL, 0);
	integer_type = pigen_data_type_integer(model);
	one = pigen_const_expr_intern_integer(model, 1, integer_type);
	zero = pigen_const_expr_intern_integer(model, 0, integer_type);
	upper = pigen_const_expr_intern_binary(model, PIGEN_BINARY_SUBTRACT,
		width, one, integer_type);
	if (upper.index == PIGEN_INVALID_ID || zero.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_data_type_id);
	dimension = (pigen_packed_dimension){upper, zero};
	result = data_type_intern(model, constructor, PIGEN_SIGN_UNSIGNED,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
		&dimension, 1);
	return result;
}
