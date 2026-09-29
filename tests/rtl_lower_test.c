/* Task 5 shape-only contract for owner-based type/expression lowering.
 *
 * It gates the landed skeleton interface:
 *   pigen_rtl_lowering { semantics, rtl, lowered-type map, lowered-expr map }
 *   pigen_rtl_lowering_init(lowering, semantics, rtl)
 *   pigen_rtl_lowering_free(lowering)
 *   pigen_lower_rtl_type(lowering, type)
 *   pigen_lower_rtl_expression(lowering, expression)
 *
 * The skeleton stubs publish nothing: init zeroes the record and leaves both
 * identity memo maps empty, and both lowering entry points return the
 * unimplemented/invalid sentinel without touching a map. The real lowering of
 * a validated source identity into an RTL record, its width/signedness/
 * child-order propagation, and its conversion and projection handling arrive
 * with the test-contract and implementation stages; this driver asserts only
 * the shape and the sentinel behavior that must never regress. */
#include <assert.h>
#include <stdio.h>

#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/source.h"

#define INVALID_TYPE ((pigen_data_type_id){PIGEN_INVALID_ID})
#define INVALID_EXPR ((pigen_const_expr_id){PIGEN_INVALID_ID})
#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)

int main(void)
{
	pigen_source_manager sources = {0};
	pigen_semantic_model sem;
	pigen_rtl_model rtl = {0};
	pigen_rtl_lowering lowering;

	pigen_semantic_init(&sem, &sources);
	pigen_rtl_lowering_init(&lowering, &sem, &rtl);

	/* init stores the models and leaves both identity memo maps empty. */
	assert(lowering.semantics == &sem);
	assert(lowering.rtl == &rtl);
	assert(!lowering.lowered_types &&
		!lowering.lowered_type_count && !lowering.lowered_type_capacity);
	assert(!lowering.lowered_expressions &&
		!lowering.lowered_expression_count &&
		!lowering.lowered_expression_capacity);

	/* Both stubs return the unimplemented/invalid sentinel and leave the maps
	 * untouched, for a valid lowering and for a NULL lowering. */
	assert(IS_INVALID_ID(pigen_lower_rtl_type(&lowering, INVALID_TYPE)));
	assert(IS_INVALID_ID(pigen_lower_rtl_expression(&lowering, INVALID_EXPR)));
	assert(IS_INVALID_ID(pigen_lower_rtl_type(NULL, INVALID_TYPE)));
	assert(IS_INVALID_ID(pigen_lower_rtl_expression(NULL, INVALID_EXPR)));
	assert(!lowering.lowered_types &&
		!lowering.lowered_type_count && !lowering.lowered_type_capacity);
	assert(!lowering.lowered_expressions &&
		!lowering.lowered_expression_count &&
		!lowering.lowered_expression_capacity);

	/* free releases the (empty) maps and zeroes the record. */
	pigen_rtl_lowering_free(&lowering);
	assert(!lowering.semantics && !lowering.rtl);
	assert(!lowering.lowered_types &&
		!lowering.lowered_type_count && !lowering.lowered_type_capacity);
	assert(!lowering.lowered_expressions &&
		!lowering.lowered_expression_count &&
		!lowering.lowered_expression_capacity);

	pigen_free_rtl_model(&rtl);
	pigen_free_semantic_model(&sem);
	pigen_free_sources(&sources);
	puts("PASS: rtl lowering skeleton inits empty identity maps");
	puts("PASS: rtl lowering stubs return the unimplemented sentinel");
	return 0;
}
