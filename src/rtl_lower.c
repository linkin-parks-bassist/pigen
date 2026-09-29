/* Owner-based type and expression lowering for the elastic RTL vertical
 * slice (Task 5 skeleton).
 *
 * This fixes only the shape of the lowering pass. The identity memo maps are
 * created empty and released on free; the actual lowering of a source data
 * type or constant expression into an RTL record, its width/signedness/
 * child-order propagation, and its conversion and projection handling are all
 * deferred to the test-contract and implementation stages. */
#include <stdlib.h>
#include <string.h>

#include "pigen/rtl_lower.h"
#include "pigen/util.h"

void pigen_rtl_lowering_init(pigen_rtl_lowering *lowering,
	pigen_semantic_model *semantics, pigen_rtl_model *rtl)
{
	*lowering = (pigen_rtl_lowering){0};
	lowering->semantics = semantics;
	lowering->rtl = rtl;
}

void pigen_rtl_lowering_free(pigen_rtl_lowering *lowering)
{
	if (!lowering)
		return;
	free(lowering->lowered_types);
	free(lowering->lowered_expressions);
	*lowering = (pigen_rtl_lowering){0};
}

pigen_rtl_type_id pigen_lower_rtl_type(pigen_rtl_lowering *lowering,
	pigen_data_type_id type)
{
	(void)lowering;
	(void)type;
	return (pigen_rtl_type_id){PIGEN_INVALID_ID};
}

pigen_rtl_expr_id pigen_lower_rtl_expression(pigen_rtl_lowering *lowering,
	pigen_const_expr_id expression)
{
	(void)lowering;
	(void)expression;
	return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
}
