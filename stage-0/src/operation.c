/* Shared semantic operation vocabulary and structural validation. */
#include "pigen/operation.h"

int pigen_unary_operator_is_valid(pigen_unary_operator operator)
{
	return operator >= PIGEN_UNARY_POSITIVE &&
		operator <= PIGEN_UNARY_REDUCTION_XNOR;
}

int pigen_binary_operator_is_valid(pigen_binary_operator operator)
{
	return operator >= PIGEN_BINARY_ADD &&
		operator <= PIGEN_BINARY_LOGICAL_OR;
}

int pigen_select_kind_is_valid(pigen_select_kind kind)
{
	return kind >= PIGEN_SEMANTIC_SELECT_RANGE &&
		kind <= PIGEN_SEMANTIC_SELECT_INDEXED_DOWN;
}
