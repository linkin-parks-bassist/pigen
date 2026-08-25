/* Canonical data-type capabilities and operation-result semantics. */
#include <stdlib.h>
#include <string.h>

#include "pigen/semantic.h"
#include "pigen/util.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

typedef enum {
	PIGEN_DATA_TYPE_INVALID,
	PIGEN_DATA_TYPE_LOGIC,
	PIGEN_DATA_TYPE_BIT,
	PIGEN_DATA_TYPE_UNSIZED_INTEGER,
	PIGEN_DATA_TYPE_SIGNED_INTEGER,
	PIGEN_DATA_TYPE_UNSIGNED_INTEGER,
	PIGEN_DATA_TYPE_EXACT_INTEGER,
	PIGEN_DATA_TYPE_BYTE,
	PIGEN_DATA_TYPE_ALIAS
} data_type_constructor;

enum {
	PIGEN_DATA_TYPE_CAPABILITY_INTEGRAL = 1u << 0
};

typedef struct {
	data_type_constructor constructor;
	const char *spelling;
	size_t base_width;
	pigen_state_domain state_domain;
	unsigned capabilities;
} primitive_data_type_descriptor;

static const primitive_data_type_descriptor primitive_data_types[] = {
	{PIGEN_DATA_TYPE_LOGIC, "logic", 1, PIGEN_DATA_TYPE_STATE_FOUR,
		PIGEN_DATA_TYPE_CAPABILITY_INTEGRAL},
	{PIGEN_DATA_TYPE_BIT, "bit", 1, PIGEN_DATA_TYPE_STATE_TWO,
		PIGEN_DATA_TYPE_CAPABILITY_INTEGRAL},
	{PIGEN_DATA_TYPE_UNSIZED_INTEGER, NULL, 32, PIGEN_DATA_TYPE_STATE_FOUR,
		PIGEN_DATA_TYPE_CAPABILITY_INTEGRAL},
	{PIGEN_DATA_TYPE_SIGNED_INTEGER, NULL, 0, PIGEN_DATA_TYPE_STATE_TWO,
		PIGEN_DATA_TYPE_CAPABILITY_INTEGRAL},
	{PIGEN_DATA_TYPE_UNSIGNED_INTEGER, NULL, 0, PIGEN_DATA_TYPE_STATE_TWO,
		PIGEN_DATA_TYPE_CAPABILITY_INTEGRAL},
	{PIGEN_DATA_TYPE_EXACT_INTEGER, NULL, 0, PIGEN_DATA_TYPE_STATE_TWO, 0},
	{PIGEN_DATA_TYPE_BYTE, NULL, 8, PIGEN_DATA_TYPE_STATE_TWO, 0}
};

struct pigen_data_type {
	data_type_constructor constructor;
	pigen_signedness signedness;
	pigen_symbol_id alias;
	pigen_data_type_id alias_target;
	pigen_const_expr_id intrinsic_width;
	pigen_integer_id exact_value;
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

static const primitive_data_type_descriptor *primitive_descriptor(
	data_type_constructor constructor)
{
	size_t i;

	for (i = 0; i < sizeof(primitive_data_types) /
		sizeof(*primitive_data_types); i++)
		if (primitive_data_types[i].constructor == constructor)
			return &primitive_data_types[i];
	return NULL;
}

static const primitive_data_type_descriptor *primitive_from_spelling(
	const pigen_semantic_model *model, pigen_source_span spelling)
{
	size_t i;

	for (i = 0; i < sizeof(primitive_data_types) /
		sizeof(*primitive_data_types); i++)
		if (primitive_data_types[i].spelling && spelling_is(model, spelling,
			primitive_data_types[i].spelling))
			return &primitive_data_types[i];
	return NULL;
}

static data_type_constructor scalar_constructor(pigen_state_domain state_domain)
{
	return state_domain == PIGEN_DATA_TYPE_STATE_TWO ? PIGEN_DATA_TYPE_BIT :
		state_domain == PIGEN_DATA_TYPE_STATE_FOUR ? PIGEN_DATA_TYPE_LOGIC :
		PIGEN_DATA_TYPE_INVALID;
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
	pigen_const_expr_id intrinsic_width, pigen_integer_id exact_value,
	const pigen_packed_dimension *dimensions,
	size_t dimension_count)
{
	size_t i;
	pigen_data_type *data_type;
	pigen_data_type_id result;

	if (!model || !model->sources ||
		(constructor != PIGEN_DATA_TYPE_ALIAS &&
			!primitive_descriptor(constructor)) ||
		signedness < PIGEN_SIGN_IMPLICIT || signedness > PIGEN_SIGN_SIGNED ||
		(dimension_count && !dimensions) ||
		model->data_type_count >= PIGEN_INVALID_ID)
		return INVALID_ID(pigen_data_type_id);
	if (constructor == PIGEN_DATA_TYPE_ALIAS)
	{
		if (alias.index == PIGEN_INVALID_ID ||
			alias_target.index == PIGEN_INVALID_ID ||
			alias_target.index >= model->data_type_count ||
			intrinsic_width.index != PIGEN_INVALID_ID ||
			exact_value.index != PIGEN_INVALID_ID)
			return INVALID_ID(pigen_data_type_id);
	}
	else if (alias.index != PIGEN_INVALID_ID ||
		alias_target.index != PIGEN_INVALID_ID ||
		((constructor == PIGEN_DATA_TYPE_SIGNED_INTEGER ||
		constructor == PIGEN_DATA_TYPE_UNSIGNED_INTEGER ||
		constructor == PIGEN_DATA_TYPE_BYTE) !=
		(intrinsic_width.index != PIGEN_INVALID_ID)) ||
		((constructor == PIGEN_DATA_TYPE_EXACT_INTEGER) !=
		(exact_value.index != PIGEN_INVALID_ID)))
		return INVALID_ID(pigen_data_type_id);
	if (intrinsic_width.index != PIGEN_INVALID_ID &&
		!pigen_const_expr_get(model, intrinsic_width))
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
			data_type->intrinsic_width.index == intrinsic_width.index &&
			data_type->exact_value.index == exact_value.index &&
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
		intrinsic_width, exact_value, model->data_type_dimension_count,
		dimension_count};
	if (dimension_count)
		memcpy(model->data_type_dimensions + model->data_type_dimension_count,
			dimensions,
			dimension_count * sizeof(*dimensions));
	model->data_type_dimension_count += dimension_count;
	return result;
}

static pigen_data_type_id scalar_data_type(pigen_semantic_model *model,
	pigen_state_domain state_domain)
{
	data_type_constructor constructor = scalar_constructor(state_domain);

	if (constructor == PIGEN_DATA_TYPE_INVALID)
		return INVALID_ID(pigen_data_type_id);
	return data_type_intern(model, constructor, PIGEN_SIGN_UNSIGNED,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
		INVALID_ID(pigen_const_expr_id), INVALID_ID(pigen_integer_id), NULL, 0);
}

static pigen_data_type_id primitive_data_type_from_spelling(
	pigen_semantic_model *model, pigen_source_span spelling,
	pigen_signedness signedness, const pigen_packed_dimension *dimensions,
	size_t dimension_count)
{
	const primitive_data_type_descriptor *descriptor;

	if (!model || !model->sources) return INVALID_ID(pigen_data_type_id);
	descriptor = primitive_from_spelling(model, spelling);
	if (!descriptor) return INVALID_ID(pigen_data_type_id);
	return data_type_intern(model, descriptor->constructor, signedness,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
		INVALID_ID(pigen_const_expr_id), INVALID_ID(pigen_integer_id),
		dimensions, dimension_count);
}

pigen_data_type_id pigen_data_type_implicit(pigen_semantic_model *model,
	pigen_signedness signedness, const pigen_packed_dimension *dimensions,
	size_t dimension_count)
{
	return data_type_intern(model, PIGEN_DATA_TYPE_LOGIC, signedness,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
		INVALID_ID(pigen_const_expr_id), INVALID_ID(pigen_integer_id),
		dimensions, dimension_count);
}

pigen_data_type_id pigen_data_type_alias(pigen_semantic_model *model,
	pigen_symbol_id alias, pigen_data_type_id target,
	pigen_signedness signedness,
	const pigen_packed_dimension *dimensions, size_t dimension_count)
{
	return data_type_intern(model, PIGEN_DATA_TYPE_ALIAS, signedness, alias,
		target, INVALID_ID(pigen_const_expr_id), INVALID_ID(pigen_integer_id),
		dimensions, dimension_count);
}

pigen_data_type_id pigen_data_type_unsized_integer(pigen_semantic_model *model)
{
	if (model->unsized_integer_data_type.index == PIGEN_INVALID_ID)
		model->unsized_integer_data_type = data_type_intern(model,
			PIGEN_DATA_TYPE_UNSIZED_INTEGER,
			PIGEN_SIGN_SIGNED, INVALID_ID(pigen_symbol_id),
			INVALID_ID(pigen_data_type_id), INVALID_ID(pigen_const_expr_id),
			INVALID_ID(pigen_integer_id), NULL, 0);
	return model->unsized_integer_data_type;
}

pigen_data_type_id pigen_data_type_boolean(pigen_semantic_model *model)
{
	if (model->boolean_data_type.index == PIGEN_INVALID_ID)
		model->boolean_data_type = data_type_intern(model,
			PIGEN_DATA_TYPE_LOGIC,
			PIGEN_SIGN_UNSIGNED, INVALID_ID(pigen_symbol_id),
			INVALID_ID(pigen_data_type_id), INVALID_ID(pigen_const_expr_id),
			INVALID_ID(pigen_integer_id), NULL, 0);
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
		const primitive_data_type_descriptor *descriptor =
			primitive_descriptor(known->constructor);
		if (known->intrinsic_width.index != PIGEN_INVALID_ID ||
			(descriptor && descriptor->base_width > 1))
			return scalar_data_type(model, descriptor->state_domain);
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
		known->alias_target, known->intrinsic_width, known->exact_value, remaining,
		remaining_count);
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
	pigen_const_expr_id intrinsic_width;
	size_t dimension_count;
	pigen_data_type_id result;

	type = packed_projection_base(model, type);
	known = data_type_get(model, type);
	if (!known) return INVALID_ID(pigen_data_type_id);
	if (!known->dimension_count)
	{
		const primitive_data_type_descriptor *descriptor =
			primitive_descriptor(known->constructor);
		if (!descriptor || (known->intrinsic_width.index == PIGEN_INVALID_ID &&
			descriptor->base_width <= 1))
			return INVALID_ID(pigen_data_type_id);
		constructor = scalar_constructor(descriptor->state_domain);
		if (constructor == PIGEN_DATA_TYPE_INVALID)
			return INVALID_ID(pigen_data_type_id);
		alias = INVALID_ID(pigen_symbol_id);
		intrinsic_width = INVALID_ID(pigen_const_expr_id);
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
		intrinsic_width = known->intrinsic_width;
		dimension_count = known->dimension_count;
		dimensions = pigen_resize(NULL,
			dimension_count * sizeof(*dimensions));
		dimensions[0] = selected;
		if (dimension_count > 1)
			memcpy(dimensions + 1, base_dimensions + 1,
				(dimension_count - 1) * sizeof(*dimensions));
	}
	result = data_type_intern(model, constructor, PIGEN_SIGN_UNSIGNED,
		alias, known->alias_target, intrinsic_width, known->exact_value, dimensions,
		dimension_count);
	free(dimensions);
	return result;
}

static pigen_const_expr_id intern_unsized_integer_binary(
	pigen_semantic_model *model, pigen_binary_operator operator,
	pigen_const_expr_id left, pigen_const_expr_id right)
{
	pigen_data_type_id data_type = pigen_data_type_unsized_integer(model);
	pigen_binary_resolution resolution;

	if (!pigen_data_type_resolve_binary_operation(model, operator, data_type,
		data_type, &resolution) ||
		resolution.left_conversion.kind != PIGEN_CONVERSION_IDENTITY ||
		resolution.right_conversion.kind != PIGEN_CONVERSION_IDENTITY)
		return INVALID_ID(pigen_const_expr_id);
	return pigen_const_expr_intern_binary(model, resolution.operation, left,
		right);
}

pigen_type_spelling_domain pigen_data_type_spelling_domain(
	const pigen_semantic_model *model, pigen_source_span spelling)
{
	if (!model || !model->sources) return PIGEN_TYPE_SPELLING_UNKNOWN;
	if (spelling_is(model, spelling, "int") ||
		spelling_is(model, spelling, "uint") ||
		spelling_is(model, spelling, "byte"))
		return PIGEN_TYPE_SPELLING_PIGEN;
	return primitive_from_spelling(model, spelling) ?
		PIGEN_TYPE_SPELLING_SYSTEMVERILOG : PIGEN_TYPE_SPELLING_UNKNOWN;
}

static pigen_packed_dimension *arguments_to_dimensions(
	pigen_semantic_model *model, const pigen_data_type_argument *arguments,
	size_t argument_count)
{
	pigen_packed_dimension *dimensions;
	pigen_const_expr_id zero;
	pigen_const_expr_id one;
	size_t i;

	if (!argument_count) return NULL;
	if (!arguments) return NULL;
	dimensions = pigen_resize(NULL, argument_count * sizeof(*dimensions));
	zero = pigen_const_expr_intern_integer(model, 0,
		pigen_data_type_unsized_integer(model));
	one = pigen_const_expr_intern_integer(model, 1,
		pigen_data_type_unsized_integer(model));
	for (i = 0; i < argument_count; i++)
	{
		if (arguments[i].kind == PIGEN_DATA_TYPE_ARGUMENT_RANGE)
		{
			dimensions[i].left = arguments[i].as.range.left;
			dimensions[i].right = arguments[i].as.range.right;
		}
		else if (arguments[i].kind == PIGEN_DATA_TYPE_ARGUMENT_COUNT)
		{
			dimensions[i].left = intern_unsized_integer_binary(model,
				PIGEN_BINARY_SUBTRACT, arguments[i].as.count, one);
			dimensions[i].right = zero;
		}
		else
		{
			free(dimensions);
			return NULL;
		}
		if (!pigen_const_expr_get(model, dimensions[i].left) ||
			!pigen_const_expr_get(model, dimensions[i].right))
		{
			free(dimensions);
			return NULL;
		}
	}
	return dimensions;
}

pigen_data_type_id pigen_data_type_from_spelling(
	pigen_semantic_model *model, pigen_source_span spelling,
	pigen_signedness signedness, const pigen_data_type_argument *arguments,
	size_t argument_count)
{
	pigen_packed_dimension *dimensions;
	pigen_data_type_id result;

	if (!model || (argument_count && !arguments))
		return INVALID_ID(pigen_data_type_id);
	if (spelling_is(model, spelling, "int") ||
		spelling_is(model, spelling, "uint"))
	{
		if (signedness != PIGEN_SIGN_IMPLICIT || argument_count != 1 ||
			arguments[0].kind != PIGEN_DATA_TYPE_ARGUMENT_COUNT)
			return INVALID_ID(pigen_data_type_id);
		return spelling_is(model, spelling, "int") ?
			pigen_data_type_signed_integer(model, arguments[0].as.count) :
			pigen_data_type_unsigned_integer(model, arguments[0].as.count);
	}
	if (spelling_is(model, spelling, "byte"))
		return signedness == PIGEN_SIGN_IMPLICIT && !argument_count ?
			pigen_data_type_byte(model) : INVALID_ID(pigen_data_type_id);
	if (!primitive_from_spelling(model, spelling))
		return INVALID_ID(pigen_data_type_id);
	dimensions = arguments_to_dimensions(model, arguments, argument_count);
	if (argument_count && !dimensions) return INVALID_ID(pigen_data_type_id);
	result = primitive_data_type_from_spelling(model, spelling, signedness,
		dimensions, argument_count);
	free(dimensions);
	return result;
}

pigen_data_type_id pigen_data_type_alias_with_arguments(
	pigen_semantic_model *model, pigen_symbol_id alias,
	pigen_data_type_id target, pigen_signedness signedness,
	const pigen_data_type_argument *arguments, size_t argument_count)
{
	pigen_packed_dimension *dimensions = arguments_to_dimensions(model,
		arguments, argument_count);
	pigen_data_type_id result;

	if (argument_count && !dimensions) return INVALID_ID(pigen_data_type_id);
	result = pigen_data_type_alias(model, alias, target, signedness, dimensions,
		argument_count);
	free(dimensions);
	return result;
}

pigen_data_type_id pigen_data_type_implicit_with_arguments(
	pigen_semantic_model *model, pigen_signedness signedness,
	const pigen_data_type_argument *arguments, size_t argument_count)
{
	pigen_packed_dimension *dimensions = arguments_to_dimensions(model,
		arguments, argument_count);
	pigen_data_type_id result;

	if (argument_count && !dimensions) return INVALID_ID(pigen_data_type_id);
	result = pigen_data_type_implicit(model, signedness, dimensions,
		argument_count);
	free(dimensions);
	return result;
}

pigen_data_type_id pigen_data_type_packed_select(pigen_semantic_model *model,
	pigen_data_type_id type, pigen_const_expr_id left,
	pigen_const_expr_id right, pigen_select_kind kind)
{
	pigen_data_type_id unsized_integer_data_type;
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
	unsized_integer_data_type = pigen_data_type_unsized_integer(model);
	width = pigen_const_expr_intern_select_width(model, left, right, kind);
	one = pigen_const_expr_intern_integer(model, 1, unsized_integer_data_type);
	zero = pigen_const_expr_intern_integer(model, 0, unsized_integer_data_type);
	upper = intern_unsized_integer_binary(model, PIGEN_BINARY_SUBTRACT,
		width, one);
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
	const primitive_data_type_descriptor *descriptor = known ?
		primitive_descriptor(known->constructor) : NULL;

	return descriptor &&
		(descriptor->capabilities & PIGEN_DATA_TYPE_CAPABILITY_INTEGRAL) != 0;
}

pigen_state_domain pigen_data_type_state_domain(
	const pigen_semantic_model *model, pigen_data_type_id type)
{
	const pigen_data_type *known = data_type_get(model,
		underlying_data_type(model, type));
	const primitive_data_type_descriptor *descriptor = known ?
		primitive_descriptor(known->constructor) : NULL;

	return descriptor ? descriptor->state_domain : PIGEN_DATA_TYPE_STATE_INVALID;
}

static int integer_width_is_valid(const pigen_semantic_model *model,
	pigen_const_expr_id width)
{
	const pigen_const_expr *known = pigen_const_expr_get(model, width);

	return known && pigen_data_type_is_integral(model, known->data_type) &&
		(known->kind != PIGEN_CONST_EXPR_INTEGER || known->as.integer != 0);
}

static pigen_data_type_id integer_data_type(pigen_semantic_model *model,
	data_type_constructor constructor, pigen_const_expr_id width)
{
	pigen_signedness signedness = constructor == PIGEN_DATA_TYPE_SIGNED_INTEGER ?
		PIGEN_SIGN_SIGNED : PIGEN_SIGN_UNSIGNED;

	if (!model || !integer_width_is_valid(model, width))
		return INVALID_ID(pigen_data_type_id);
	return data_type_intern(model, constructor, signedness,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id), width,
		INVALID_ID(pigen_integer_id), NULL, 0);
}

pigen_data_type_id pigen_data_type_signed_integer(pigen_semantic_model *model,
	pigen_const_expr_id width)
{
	return integer_data_type(model, PIGEN_DATA_TYPE_SIGNED_INTEGER, width);
}

pigen_data_type_id pigen_data_type_unsigned_integer(
	pigen_semantic_model *model, pigen_const_expr_id width)
{
	return integer_data_type(model, PIGEN_DATA_TYPE_UNSIGNED_INTEGER, width);
}

pigen_data_type_id pigen_data_type_exact_integer(
	pigen_semantic_model *model, pigen_integer_id value)
{
	if (!model || value.index == PIGEN_INVALID_ID ||
		value.index >= model->integer_count)
		return INVALID_ID(pigen_data_type_id);
	return data_type_intern(model, PIGEN_DATA_TYPE_EXACT_INTEGER,
		PIGEN_SIGN_IMPLICIT, INVALID_ID(pigen_symbol_id),
		INVALID_ID(pigen_data_type_id), INVALID_ID(pigen_const_expr_id), value,
		NULL, 0);
}

pigen_integer_id pigen_data_type_exact_value(
	const pigen_semantic_model *model, pigen_data_type_id data_type)
{
	const pigen_data_type *known = data_type_get(model,
		underlying_data_type(model, data_type));

	return known && known->constructor == PIGEN_DATA_TYPE_EXACT_INTEGER ?
		known->exact_value : INVALID_ID(pigen_integer_id);
}

pigen_data_type_id pigen_data_type_byte(pigen_semantic_model *model)
{
	const primitive_data_type_descriptor *descriptor =
		primitive_descriptor(PIGEN_DATA_TYPE_BYTE);
	pigen_const_expr_id width;

	if (!model || !descriptor) return INVALID_ID(pigen_data_type_id);
	width = pigen_const_expr_intern_integer(model, descriptor->base_width,
		pigen_data_type_unsized_integer(model));
	if (width.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_data_type_id);
	return data_type_intern(model, PIGEN_DATA_TYPE_BYTE, PIGEN_SIGN_UNSIGNED,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id), width,
		INVALID_ID(pigen_integer_id), NULL, 0);
}

pigen_numerical_interpretation pigen_data_type_numerical_interpretation(
	const pigen_semantic_model *model, pigen_data_type_id data_type)
{
	const pigen_data_type *known = data_type_get(model,
		underlying_data_type(model, data_type));

	if (!known) return PIGEN_NUMERICAL_INVALID;
	if (known->constructor == PIGEN_DATA_TYPE_SIGNED_INTEGER)
		return PIGEN_NUMERICAL_SIGNED_INTEGER;
	if (known->constructor == PIGEN_DATA_TYPE_UNSIGNED_INTEGER)
		return PIGEN_NUMERICAL_UNSIGNED_INTEGER;
	if (known->constructor == PIGEN_DATA_TYPE_EXACT_INTEGER)
		return PIGEN_NUMERICAL_EXACT_INTEGER;
	if (known->constructor == PIGEN_DATA_TYPE_BYTE)
		return PIGEN_NUMERICAL_NONE;
	return PIGEN_NUMERICAL_SYSTEMVERILOG;
}

static int data_type_is_byte(const pigen_semantic_model *model,
	pigen_data_type_id data_type)
{
	const pigen_data_type *known = data_type_get(model,
		underlying_data_type(model, data_type));

	return known && known->constructor == PIGEN_DATA_TYPE_BYTE;
}

static int data_type_is_pigen_integer(const pigen_semantic_model *model,
	pigen_data_type_id data_type)
{
	pigen_numerical_interpretation interpretation =
		pigen_data_type_numerical_interpretation(model, data_type);

	return interpretation == PIGEN_NUMERICAL_SIGNED_INTEGER ||
		interpretation == PIGEN_NUMERICAL_UNSIGNED_INTEGER ||
		interpretation == PIGEN_NUMERICAL_EXACT_INTEGER;
}

static int conversion_resolve(const pigen_semantic_model *model,
	pigen_data_type_id source, pigen_data_type_id target,
	pigen_conversion_kind kind, pigen_conversion *conversion)
{
	if (!conversion || !data_type_get(model, source) ||
		!data_type_get(model, target))
		return 0;
	*conversion = (pigen_conversion){kind, source, target};
	return 1;
}

int pigen_data_type_resolve_assignment_conversion(
	const pigen_semantic_model *model, pigen_data_type_id source,
	pigen_data_type_id target, pigen_conversion *conversion)
{
	pigen_numerical_interpretation source_interpretation;
	pigen_numerical_interpretation target_interpretation;

	if (!conversion || !data_type_get(model, source) ||
		!data_type_get(model, target))
		return 0;
	if (source.index == target.index)
		return conversion_resolve(model, source, target,
			PIGEN_CONVERSION_IDENTITY, conversion);
	source_interpretation = pigen_data_type_numerical_interpretation(model,
		source);
	target_interpretation = pigen_data_type_numerical_interpretation(model,
		target);
	if (source_interpretation == PIGEN_NUMERICAL_EXACT_INTEGER &&
		(target_interpretation == PIGEN_NUMERICAL_SIGNED_INTEGER ||
		target_interpretation == PIGEN_NUMERICAL_UNSIGNED_INTEGER))
		return conversion_resolve(model, source, target,
			PIGEN_CONVERSION_EXACT_INTEGER, conversion);
	if (data_type_is_pigen_integer(model, source) &&
		data_type_is_pigen_integer(model, target) &&
		source_interpretation == target_interpretation)
		return conversion_resolve(model, source, target,
			PIGEN_CONVERSION_INTEGER_RESIZE, conversion);
	return 0;
}

int pigen_data_type_resolve_explicit_conversion(
	const pigen_semantic_model *model, pigen_data_type_id source,
	pigen_data_type_id target, pigen_conversion *conversion)
{
	pigen_numerical_interpretation source_interpretation;
	pigen_numerical_interpretation target_interpretation;

	if (!conversion || !data_type_get(model, source) ||
		!data_type_get(model, target))
		return 0;
	if (source.index == target.index)
		return conversion_resolve(model, source, target,
			PIGEN_CONVERSION_IDENTITY, conversion);
	source_interpretation = pigen_data_type_numerical_interpretation(model,
		source);
	target_interpretation = pigen_data_type_numerical_interpretation(model,
		target);
	if (source_interpretation == PIGEN_NUMERICAL_EXACT_INTEGER &&
		target_interpretation != PIGEN_NUMERICAL_EXACT_INTEGER &&
		(target_interpretation == PIGEN_NUMERICAL_SIGNED_INTEGER ||
		target_interpretation == PIGEN_NUMERICAL_UNSIGNED_INTEGER ||
		data_type_is_byte(model, target) ||
		pigen_data_type_is_integral(model, target)))
		return conversion_resolve(model, source, target,
			PIGEN_CONVERSION_EXACT_INTEGER, conversion);
	if (data_type_is_pigen_integer(model, source) &&
		data_type_is_pigen_integer(model, target))
		return conversion_resolve(model, source, target,
			source_interpretation == target_interpretation ?
			PIGEN_CONVERSION_INTEGER_RESIZE :
			PIGEN_CONVERSION_INTEGER_REINTERPRET, conversion);
	if (data_type_is_byte(model, source) &&
		data_type_is_pigen_integer(model, target))
		return conversion_resolve(model, source, target,
			PIGEN_CONVERSION_VECTOR_TO_INTEGER, conversion);
	if (data_type_is_pigen_integer(model, source) &&
		data_type_is_byte(model, target))
		return conversion_resolve(model, source, target,
			PIGEN_CONVERSION_INTEGER_TO_VECTOR, conversion);
	return 0;
}

pigen_data_type_id pigen_data_type_sized_logic(pigen_semantic_model *model,
	size_t width, pigen_signedness signedness)
{
	pigen_packed_dimension dimension;
	pigen_const_expr_id left;
	pigen_const_expr_id right;
	pigen_data_type_id unsized_integer_data_type;

	if (!model || !width || (signedness != PIGEN_SIGN_SIGNED &&
		signedness != PIGEN_SIGN_UNSIGNED))
		return INVALID_ID(pigen_data_type_id);
	if (width == 1)
		return data_type_intern(model, PIGEN_DATA_TYPE_LOGIC, signedness,
			INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
			INVALID_ID(pigen_const_expr_id), INVALID_ID(pigen_integer_id),
			NULL, 0);
	unsized_integer_data_type = pigen_data_type_unsized_integer(model);
	left = pigen_const_expr_intern_integer(model, (uint64_t)(width - 1),
		unsized_integer_data_type);
	right = pigen_const_expr_intern_integer(model, 0, unsized_integer_data_type);
	if (left.index == PIGEN_INVALID_ID || right.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_data_type_id);
	dimension = (pigen_packed_dimension){left, right};
	return data_type_intern(model, PIGEN_DATA_TYPE_LOGIC, signedness,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
		INVALID_ID(pigen_const_expr_id), INVALID_ID(pigen_integer_id),
		&dimension, 1);
}

static int unary_boolean_result(pigen_unary_operator operator)
{
	return operator == PIGEN_UNARY_LOGICAL_NOT ||
		(operator >= PIGEN_UNARY_REDUCTION_AND &&
		operator <= PIGEN_UNARY_REDUCTION_XNOR);
}

static int unary_arithmetic_operator(pigen_unary_operator operator)
{
	return operator == PIGEN_UNARY_POSITIVE ||
		operator == PIGEN_UNARY_NEGATE;
}

static int unary_reduction_operator(pigen_unary_operator operator)
{
	return operator >= PIGEN_UNARY_REDUCTION_AND &&
		operator <= PIGEN_UNARY_REDUCTION_XNOR;
}

static int binary_arithmetic_operator(pigen_binary_operator operator)
{
	return operator >= PIGEN_BINARY_ADD && operator <= PIGEN_BINARY_POWER;
}

static int binary_shift_operator(pigen_binary_operator operator)
{
	return operator >= PIGEN_BINARY_SHIFT_LEFT &&
		operator <= PIGEN_BINARY_ARITH_SHIFT_RIGHT;
}

static int binary_ordered_operator(pigen_binary_operator operator)
{
	return operator >= PIGEN_BINARY_LESS &&
		operator <= PIGEN_BINARY_GREATER_EQUAL;
}

static int binary_equality_operator(pigen_binary_operator operator)
{
	return operator >= PIGEN_BINARY_EQUAL &&
		operator <= PIGEN_BINARY_WILDCARD_NOT_EQUAL;
}

static int binary_bitwise_operator(pigen_binary_operator operator)
{
	return operator >= PIGEN_BINARY_BITWISE_AND &&
		operator <= PIGEN_BINARY_BITWISE_OR;
}

static int binary_logical_operator(pigen_binary_operator operator)
{
	return operator == PIGEN_BINARY_LOGICAL_AND ||
		operator == PIGEN_BINARY_LOGICAL_OR;
}

static int numerical_is_concrete(pigen_numerical_interpretation interpretation)
{
	return interpretation == PIGEN_NUMERICAL_SIGNED_INTEGER ||
		interpretation == PIGEN_NUMERICAL_UNSIGNED_INTEGER;
}

static int exact_is_negative(const pigen_semantic_model *model,
	pigen_data_type_id type)
{
	pigen_integer_id value = pigen_data_type_exact_value(model, type);

	return value.index != PIGEN_INVALID_ID && model->integers[value.index].negative;
}

static int exact_to_size(const pigen_semantic_model *model,
	pigen_data_type_id type, size_t *value)
{
	pigen_integer_id id = pigen_data_type_exact_value(model, type);
	const pigen_integer *known;

	if (!value || id.index == PIGEN_INVALID_ID) return 0;
	known = &model->integers[id.index];
	if (known->negative || known->limb_count > 1) return 0;
	*value = known->limb_count ? model->integer_limbs[known->first_limb] : 0;
	return 1;
}

static pigen_const_expr_id width_constant(pigen_semantic_model *model,
	size_t value)
{
	return pigen_const_expr_intern_integer(model, value,
		pigen_data_type_unsized_integer(model));
}

static pigen_const_expr_id width_sum(pigen_semantic_model *model,
	pigen_const_expr_id left, pigen_const_expr_id right)
{
	pigen_const_expr_id terms[2] = {left, right};

	return pigen_const_expr_intern_width_sum(model, terms, 2);
}

static pigen_const_expr_id width_plus_one(pigen_semantic_model *model,
	pigen_const_expr_id width)
{
	return width_sum(model, width, width_constant(model, 1));
}

static pigen_const_expr_id maximum_unsigned_value_expression(
	pigen_semantic_model *model, pigen_const_expr_id width)
{
	pigen_const_expr_id one = width_constant(model, 1);
	pigen_const_expr_id two = width_constant(model, 2);
	pigen_const_expr_id power = intern_unsized_integer_binary(model,
		PIGEN_BINARY_POWER, two, width);

	if (one.index == PIGEN_INVALID_ID || two.index == PIGEN_INVALID_ID ||
		power.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_const_expr_id);
	return intern_unsized_integer_binary(model, PIGEN_BINARY_SUBTRACT, power,
		one);
}

static pigen_data_type_id integer_with_width(pigen_semantic_model *model,
	int is_signed, pigen_const_expr_id width)
{
	return is_signed ? pigen_data_type_signed_integer(model, width) :
		pigen_data_type_unsigned_integer(model, width);
}

static pigen_const_expr_id exact_width(pigen_semantic_model *model,
	pigen_data_type_id type, int is_signed)
{
	pigen_integer_id value = pigen_data_type_exact_value(model, type);
	size_t width = is_signed ? pigen_integer_signed_width(model, value) :
		pigen_integer_unsigned_width(model, value);

	return width ? width_constant(model, width) :
		INVALID_ID(pigen_const_expr_id);
}

static pigen_data_type_id exact_representation(pigen_semantic_model *model,
	pigen_data_type_id type)
{
	int is_signed = exact_is_negative(model, type);
	pigen_const_expr_id width = exact_width(model, type, is_signed);

	return integer_with_width(model, is_signed, width);
}

static int resolve_operand_conversion(const pigen_semantic_model *model,
	pigen_data_type_id source, pigen_data_type_id target,
	pigen_conversion *conversion)
{
	pigen_numerical_interpretation source_interpretation =
		pigen_data_type_numerical_interpretation(model, source);
	pigen_numerical_interpretation target_interpretation =
		pigen_data_type_numerical_interpretation(model, target);

	if (source.index == target.index)
		return conversion_resolve(model, source, target,
			PIGEN_CONVERSION_IDENTITY, conversion);
	if (source_interpretation == PIGEN_NUMERICAL_EXACT_INTEGER &&
		numerical_is_concrete(target_interpretation))
		return conversion_resolve(model, source, target,
			PIGEN_CONVERSION_EXACT_INTEGER, conversion);
	if (numerical_is_concrete(source_interpretation) &&
		numerical_is_concrete(target_interpretation))
		return conversion_resolve(model, source, target,
			source_interpretation == target_interpretation ?
			PIGEN_CONVERSION_INTEGER_RESIZE :
			PIGEN_CONVERSION_INTEGER_PROMOTION, conversion);
	return pigen_data_type_resolve_assignment_conversion(model, source, target,
		conversion);
}

static int common_numerical_type(pigen_semantic_model *model,
	pigen_data_type_id left, pigen_data_type_id right,
	pigen_data_type_id *common)
{
	pigen_data_type_id operands[2] = {left, right};
	pigen_const_expr_id widths[2];
	pigen_numerical_interpretation interpretations[2];
	int is_signed;
	size_t i;

	if (!common) return 0;
	for (i = 0; i < 2; i++)
	{
		interpretations[i] = pigen_data_type_numerical_interpretation(model,
			operands[i]);
		if (!data_type_is_pigen_integer(model, operands[i])) return 0;
	}
	if (interpretations[0] == PIGEN_NUMERICAL_EXACT_INTEGER &&
		interpretations[1] == PIGEN_NUMERICAL_EXACT_INTEGER)
		return 0;
	is_signed = interpretations[0] == PIGEN_NUMERICAL_SIGNED_INTEGER ||
		interpretations[1] == PIGEN_NUMERICAL_SIGNED_INTEGER ||
		exact_is_negative(model, left) || exact_is_negative(model, right);
	for (i = 0; i < 2; i++)
	{
		if (interpretations[i] == PIGEN_NUMERICAL_EXACT_INTEGER)
			widths[i] = exact_width(model, operands[i], is_signed);
		else
		{
			widths[i] = pigen_data_type_packed_width(model, operands[i]);
			if (is_signed && interpretations[i] ==
				PIGEN_NUMERICAL_UNSIGNED_INTEGER)
				widths[i] = width_plus_one(model, widths[i]);
		}
		if (widths[i].index == PIGEN_INVALID_ID) return 0;
	}
	*common = integer_with_width(model, is_signed,
		pigen_const_expr_intern_width_maximum(model, widths, 2));
	return common->index != PIGEN_INVALID_ID;
}

static int exact_is_zero(const pigen_semantic_model *model,
	pigen_data_type_id type)
{
	pigen_integer_id value = pigen_data_type_exact_value(model, type);

	return value.index != PIGEN_INVALID_ID && pigen_integer_is_zero(model, value);
}

static int exact_is_one(const pigen_semantic_model *model,
	pigen_data_type_id type)
{
	pigen_integer_id value = pigen_data_type_exact_value(model, type);
	const pigen_integer *known;

	if (value.index == PIGEN_INVALID_ID) return 0;
	known = &model->integers[value.index];
	return !known->negative && known->limb_count == 1 &&
		model->integer_limbs[known->first_limb] == 1;
}

static int data_type_admits_logical_use(const pigen_semantic_model *model,
	pigen_data_type_id data_type)
{
	return pigen_data_type_is_integral(model, data_type) ||
		data_type_is_byte(model, data_type) ||
		pigen_data_type_numerical_interpretation(model, data_type) ==
			PIGEN_NUMERICAL_EXACT_INTEGER;
}

int pigen_data_type_resolve_unary_operation(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_data_type_id operand,
	pigen_unary_resolution *resolution)
{
	pigen_unary_resolution resolved;
	pigen_data_type_id result;
	pigen_numerical_interpretation interpretation;

	if (!model || !resolution || !pigen_unary_operator_is_valid(operator) ||
		!data_type_get(model, operand))
		return 0;
	interpretation = pigen_data_type_numerical_interpretation(model, operand);
	if (interpretation == PIGEN_NUMERICAL_EXACT_INTEGER)
	{
		pigen_integer_id value = pigen_data_type_exact_value(model, operand);

		if (operator == PIGEN_UNARY_POSITIVE) result = operand;
		else if (operator == PIGEN_UNARY_NEGATE)
			result = pigen_data_type_exact_integer(model,
				pigen_integer_negate(model, value));
		else if (operator == PIGEN_UNARY_LOGICAL_NOT)
			result = pigen_data_type_boolean(model);
		else return 0;
	}
	else if (numerical_is_concrete(interpretation))
	{
		if (operator == PIGEN_UNARY_NEGATE)
			result = pigen_data_type_signed_integer(model, width_plus_one(model,
				pigen_data_type_packed_width(model, operand)));
		else if (unary_arithmetic_operator(operator) ||
			operator == PIGEN_UNARY_BITWISE_NOT)
			result = operand;
		else if (unary_boolean_result(operator))
			result = pigen_data_type_boolean(model);
		else return 0;
	}
	else if (data_type_is_byte(model, operand))
	{
		if (operator != PIGEN_UNARY_BITWISE_NOT &&
			operator != PIGEN_UNARY_LOGICAL_NOT &&
			!unary_reduction_operator(operator))
			return 0;
		result = unary_boolean_result(operator) ? pigen_data_type_boolean(model) :
			operand;
	}
	else if (!pigen_data_type_is_integral(model, operand))
		return 0;
	else result = unary_boolean_result(operator) ? pigen_data_type_boolean(model) :
		operand;
	if (!conversion_resolve(model, operand, operand, PIGEN_CONVERSION_IDENTITY,
		&resolved.operand_conversion))
		return 0;
	if (result.index == PIGEN_INVALID_ID) return 0;
	resolved.operation = (pigen_unary_operation){operator,
		operand, result};
	*resolution = resolved;
	return 1;
}

static int binary_boolean_result(pigen_binary_operator operator)
{
	return (operator >= PIGEN_BINARY_LESS &&
		operator <= PIGEN_BINARY_WILDCARD_NOT_EQUAL) ||
		operator == PIGEN_BINARY_LOGICAL_AND ||
		operator == PIGEN_BINARY_LOGICAL_OR;
}

int pigen_data_type_resolve_binary_operation(pigen_semantic_model *model,
	pigen_binary_operator operator, pigen_data_type_id left,
	pigen_data_type_id right, pigen_binary_resolution *resolution)
{
	pigen_binary_resolution resolved;
	pigen_data_type_id effective_left = left;
	pigen_data_type_id effective_right = right;
	pigen_data_type_id result;
	pigen_numerical_interpretation left_interpretation;
	pigen_numerical_interpretation right_interpretation;

	if (!model || !resolution || !pigen_binary_operator_is_valid(operator) ||
		!data_type_get(model, left) || !data_type_get(model, right))
		return 0;
	left_interpretation = pigen_data_type_numerical_interpretation(model, left);
	right_interpretation = pigen_data_type_numerical_interpretation(model, right);
	if (left_interpretation == PIGEN_NUMERICAL_EXACT_INTEGER &&
		right_interpretation == PIGEN_NUMERICAL_EXACT_INTEGER)
	{
		pigen_integer_id left_value = pigen_data_type_exact_value(model, left);
		pigen_integer_id right_value = pigen_data_type_exact_value(model, right);
		pigen_integer_id value;

		if (operator == PIGEN_BINARY_ADD)
			value = pigen_integer_add(model, left_value, right_value);
		else if (operator == PIGEN_BINARY_SUBTRACT)
			value = pigen_integer_subtract(model, left_value, right_value);
		else if (operator == PIGEN_BINARY_MULTIPLY)
			value = pigen_integer_multiply(model, left_value, right_value);
		else if (binary_ordered_operator(operator) ||
			binary_equality_operator(operator) || binary_logical_operator(operator))
		{
			value = INVALID_ID(pigen_integer_id);
			result = pigen_data_type_boolean(model);
		}
		else return 0;
		if (operator == PIGEN_BINARY_ADD || operator == PIGEN_BINARY_SUBTRACT ||
			operator == PIGEN_BINARY_MULTIPLY)
			result = pigen_data_type_exact_integer(model, value);
	}
	else if (data_type_is_pigen_integer(model, left) ||
		data_type_is_pigen_integer(model, right))
	{
		pigen_data_type_id common;

		if (!data_type_is_pigen_integer(model, left) ||
			!data_type_is_pigen_integer(model, right) ||
			(!binary_arithmetic_operator(operator) &&
			!binary_ordered_operator(operator) &&
			!binary_equality_operator(operator) &&
			!binary_bitwise_operator(operator) &&
			!binary_logical_operator(operator) &&
			!binary_shift_operator(operator)))
			return 0;
		if (operator == PIGEN_BINARY_DIVIDE || operator == PIGEN_BINARY_MODULO)
			return 0;
		if (binary_shift_operator(operator))
		{
			pigen_const_expr_id result_width;
			pigen_const_expr_id maximum_amount;
			size_t amount;

			if (!numerical_is_concrete(left_interpretation) ||
				(right_interpretation != PIGEN_NUMERICAL_UNSIGNED_INTEGER &&
				right_interpretation != PIGEN_NUMERICAL_EXACT_INTEGER) ||
				(right_interpretation == PIGEN_NUMERICAL_EXACT_INTEGER &&
				!exact_to_size(model, right, &amount)))
				return 0;
			result = left;
			if (operator == PIGEN_BINARY_SHIFT_LEFT ||
				operator == PIGEN_BINARY_ARITH_SHIFT_LEFT)
			{
				maximum_amount = right_interpretation ==
					PIGEN_NUMERICAL_EXACT_INTEGER ? width_constant(model, amount) :
					maximum_unsigned_value_expression(model,
						pigen_data_type_packed_width(model, right));
				result_width = width_sum(model,
					pigen_data_type_packed_width(model, left),
					maximum_amount);
				result = integer_with_width(model,
					left_interpretation == PIGEN_NUMERICAL_SIGNED_INTEGER,
					result_width);
			}
		}
		else if (operator == PIGEN_BINARY_POWER)
		{
			pigen_const_expr_id factors[2];
			pigen_const_expr_id maximum_exponent;
			size_t exponent;

			if (!numerical_is_concrete(left_interpretation) ||
				(right_interpretation != PIGEN_NUMERICAL_EXACT_INTEGER &&
				right_interpretation != PIGEN_NUMERICAL_UNSIGNED_INTEGER) ||
				(right_interpretation == PIGEN_NUMERICAL_EXACT_INTEGER &&
				!exact_to_size(model, right, &exponent)))
				return 0;
			factors[0] = pigen_data_type_packed_width(model, left);
			maximum_exponent = right_interpretation ==
				PIGEN_NUMERICAL_EXACT_INTEGER ?
				width_constant(model, exponent ? exponent : 1) :
				maximum_unsigned_value_expression(model,
					pigen_data_type_packed_width(model, right));
			factors[1] = maximum_exponent;
			result = integer_with_width(model,
				left_interpretation == PIGEN_NUMERICAL_SIGNED_INTEGER,
				pigen_const_expr_intern_width_product(model, factors, 2));
		}
		else if (operator == PIGEN_BINARY_MULTIPLY)
		{
			pigen_const_expr_id widths[2];
			int result_signed = left_interpretation ==
				PIGEN_NUMERICAL_SIGNED_INTEGER || right_interpretation ==
				PIGEN_NUMERICAL_SIGNED_INTEGER || exact_is_negative(model, left) ||
				exact_is_negative(model, right);

			if (left_interpretation == PIGEN_NUMERICAL_EXACT_INTEGER)
				effective_left = exact_representation(model, left);
			if (right_interpretation == PIGEN_NUMERICAL_EXACT_INTEGER)
				effective_right = exact_representation(model, right);
			widths[0] = pigen_data_type_packed_width(model, effective_left);
			widths[1] = pigen_data_type_packed_width(model, effective_right);
			if (exact_is_zero(model, left) || exact_is_one(model, left))
				result = right;
			else if (exact_is_zero(model, right) || exact_is_one(model, right))
				result = left;
			else result = integer_with_width(model, result_signed,
				pigen_const_expr_intern_width_sum(model, widths, 2));
		}
		else
		{
			if (!common_numerical_type(model, left, right, &common)) return 0;
			effective_left = common;
			effective_right = common;
			if (operator == PIGEN_BINARY_ADD &&
				(exact_is_zero(model, left) || exact_is_zero(model, right)))
				result = exact_is_zero(model, left) ? right : left;
			else if (operator == PIGEN_BINARY_SUBTRACT && exact_is_zero(model, right))
				result = left;
			else if (operator == PIGEN_BINARY_ADD)
				result = integer_with_width(model,
					pigen_data_type_numerical_interpretation(model, common) ==
						PIGEN_NUMERICAL_SIGNED_INTEGER,
					width_plus_one(model,
						pigen_data_type_packed_width(model, common)));
			else if (operator == PIGEN_BINARY_SUBTRACT)
				result = pigen_data_type_signed_integer(model, width_plus_one(model,
					pigen_data_type_packed_width(model, common)));
			else result = binary_boolean_result(operator) ?
				pigen_data_type_boolean(model) : common;
		}
	}
	else if (data_type_is_byte(model, left) || data_type_is_byte(model, right))
	{
		if (!data_type_is_byte(model, left) || !data_type_is_byte(model, right) ||
			(!binary_bitwise_operator(operator) &&
			!binary_equality_operator(operator) &&
			!binary_logical_operator(operator)))
			return 0;
		result = binary_boolean_result(operator) ?
			pigen_data_type_boolean(model) :
			left.index == right.index ? left : pigen_data_type_byte(model);
	}
	else
	{
		if (!pigen_data_type_is_integral(model, left) ||
			!pigen_data_type_is_integral(model, right))
			return 0;
		result = binary_boolean_result(operator) ?
			pigen_data_type_boolean(model) :
			left.index == right.index ? left : INVALID_ID(pigen_data_type_id);
	}
	if (result.index == PIGEN_INVALID_ID) return 0;
	if (!resolve_operand_conversion(model, left,
		effective_left, &resolved.left_conversion) ||
		!resolve_operand_conversion(model, right,
		effective_right, &resolved.right_conversion))
		return 0;
	resolved.operation = (pigen_binary_operation){operator,
		resolved.left_conversion.target_data_type,
		resolved.right_conversion.target_data_type, result};
	*resolution = resolved;
	return 1;
}

int pigen_data_type_resolve_conditional_operation(
	pigen_semantic_model *model, pigen_data_type_id condition,
	pigen_data_type_id when_true, pigen_data_type_id when_false,
	pigen_conditional_resolution *resolution)
{
	pigen_conditional_resolution resolved;
	pigen_data_type_id effective_true = when_true;
	pigen_data_type_id effective_false = when_false;
	pigen_data_type_id result;

	if (!model || !resolution || !data_type_get(model, condition) ||
		!data_type_get(model, when_true) || !data_type_get(model, when_false) ||
		!data_type_admits_logical_use(model, condition))
		return 0;
	if (data_type_is_pigen_integer(model, when_true) ||
		data_type_is_pigen_integer(model, when_false))
	{
		if (pigen_data_type_numerical_interpretation(model, when_true) ==
			PIGEN_NUMERICAL_EXACT_INTEGER &&
			pigen_data_type_numerical_interpretation(model, when_false) ==
			PIGEN_NUMERICAL_EXACT_INTEGER)
		{
			pigen_const_expr_id widths[2];
			int is_signed;

			if (when_true.index == when_false.index) result = when_true;
			else
			{
				is_signed = exact_is_negative(model, when_true) ||
					exact_is_negative(model, when_false);
				widths[0] = exact_width(model, when_true, is_signed);
				widths[1] = exact_width(model, when_false, is_signed);
				result = integer_with_width(model, is_signed,
					pigen_const_expr_intern_width_maximum(model, widths, 2));
				effective_true = result;
				effective_false = result;
			}
		}
		else
		{
			if (!common_numerical_type(model, when_true, when_false,
				&effective_true))
				return 0;
			effective_false = effective_true;
			result = effective_true;
		}
	}
	else if (data_type_is_byte(model, when_true) ||
		data_type_is_byte(model, when_false))
	{
		if (!data_type_is_byte(model, when_true) ||
			!data_type_is_byte(model, when_false))
			return 0;
		result = when_true.index == when_false.index ? when_true :
			pigen_data_type_byte(model);
	}
	else
	{
		if (when_true.index != when_false.index) return 0;
		result = when_true;
	}
	if (!pigen_data_type_resolve_assignment_conversion(model, condition,
		condition, &resolved.condition_conversion) ||
		!resolve_operand_conversion(model, when_true,
			effective_true, &resolved.when_true_conversion) ||
		!resolve_operand_conversion(model, when_false,
			effective_false, &resolved.when_false_conversion))
		return 0;
	resolved.operation = (pigen_conditional_operation){
		resolved.condition_conversion.target_data_type,
		resolved.when_true_conversion.target_data_type,
		resolved.when_false_conversion.target_data_type, result};
	*resolution = resolved;
	return 1;
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
	if (known->constructor == PIGEN_DATA_TYPE_EXACT_INTEGER)
		return INVALID_ID(pigen_const_expr_id);
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
	else if (known->intrinsic_width.index != PIGEN_INVALID_ID)
		factors[factor_count] = known->intrinsic_width;
	else
	{
		const primitive_data_type_descriptor *descriptor =
			primitive_descriptor(known->constructor);
		if (!descriptor)
		{
			free(factors);
			return INVALID_ID(pigen_const_expr_id);
		}
		factors[factor_count] = pigen_const_expr_intern_integer(model,
			descriptor->base_width,
			pigen_data_type_unsized_integer(model));
	}
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
	pigen_data_type_id unsized_integer_data_type;
	data_type_constructor constructor;
	pigen_state_domain result_state = PIGEN_DATA_TYPE_STATE_TWO;
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
			result_state = PIGEN_DATA_TYPE_STATE_FOUR;
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
		return scalar_data_type(model, result_state);
	constructor = scalar_constructor(result_state);
	if (constructor == PIGEN_DATA_TYPE_INVALID)
		return INVALID_ID(pigen_data_type_id);
	unsized_integer_data_type = pigen_data_type_unsized_integer(model);
	one = pigen_const_expr_intern_integer(model, 1, unsized_integer_data_type);
	zero = pigen_const_expr_intern_integer(model, 0, unsized_integer_data_type);
	upper = intern_unsized_integer_binary(model, PIGEN_BINARY_SUBTRACT,
		width, one);
	if (upper.index == PIGEN_INVALID_ID || zero.index == PIGEN_INVALID_ID)
		return INVALID_ID(pigen_data_type_id);
	dimension = (pigen_packed_dimension){upper, zero};
	result = data_type_intern(model, constructor, PIGEN_SIGN_UNSIGNED,
		INVALID_ID(pigen_symbol_id), INVALID_ID(pigen_data_type_id),
		INVALID_ID(pigen_const_expr_id), INVALID_ID(pigen_integer_id),
		&dimension, 1);
	return result;
}
