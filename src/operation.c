/* Shared semantic operation vocabulary and structural validation. */
#include "pigen/operation.h"

int pigen_select_kind_is_valid(pigen_select_kind kind)
{
	return kind >= PIGEN_SEMANTIC_SELECT_RANGE &&
		kind <= PIGEN_SEMANTIC_SELECT_INDEXED_DOWN;
}
