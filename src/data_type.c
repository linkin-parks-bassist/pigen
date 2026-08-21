/* Canonical data-type capabilities and operation-result semantics. */
#include "pigen/semantic.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

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
