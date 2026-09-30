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

	/* (3) Type lowering preserves the owner-reported state, width, signedness
	 * and range shape. This is the first type-family section of the Task 5
	 * test-contract chain. Staged red: the invalid-id sentinel assert below is
	 * the section's first assert and passes against the stub (which always
	 * returns the sentinel); the first deliberate red is case 1's
	 * `!IS_INVALID_ID(id)`, since the stub still returns the sentinel for a
	 * valid type. Cases 1-3 build real owner data types and assert the lowered
	 * pigen_rtl_type record matches exactly what the data-type owner reports
	 * (signedness, state domain, width and packed range shape); case 4 pins the
	 * invalid-id contract. Only the returned record is asserted - never an
	 * imagined internal representation - so the implementer cannot shortcut the
	 * preservation contract. */
	{
		pigen_data_type_id unsized;
		pigen_const_expr_id width8;
		pigen_const_expr_id width12;
		pigen_data_type_id t_signed;
		pigen_data_type_id t_unsigned;
		pigen_data_type_id t_bit;
		pigen_rtl_type_id id;
		const pigen_rtl_type *rt;
		uint64_t w;

		/* Build the three owner data types through the owner APIs. The width
		 * expressions are concrete non-zero integers typed with the owner's
		 * unsized integer type, so each data type is valid. */
		unsized = pigen_data_type_unsized_integer(&sem);
		assert(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem, 8, unsized);
		width12 = pigen_const_expr_intern_integer(&sem, 12, unsized);
		t_signed = pigen_data_type_signed_integer(&sem, width8);
		t_unsigned = pigen_data_type_unsigned_integer(&sem, width12);
		t_bit = pigen_data_type_sized_logic(&sem, 16, PIGEN_SIGN_UNSIGNED);
		assert(!IS_INVALID_ID(width8) && !IS_INVALID_ID(width12));
		assert(!IS_INVALID_ID(t_signed) && !IS_INVALID_ID(t_unsigned) &&
			!IS_INVALID_ID(t_bit));

		/* Case 4 (front-loaded, the section's first behavioral assert): an
		 * invalid data-type id returns the RTL-type sentinel and leaves both
		 * identity memo maps untouched. */
		id = pigen_lower_rtl_type(&lowering, INVALID_TYPE);
		assert(IS_INVALID_ID(id));
		assert(!lowering.lowered_types &&
			!lowering.lowered_type_count && !lowering.lowered_type_capacity);
		assert(!lowering.lowered_expressions &&
			!lowering.lowered_expression_count &&
			!lowering.lowered_expression_capacity);

		/* Case 1: signed int[8] preserves signedness, state domain, width and
		 * the owner-reported range shape (dimension count, packed width and a
		 * valid packed element). */
		id = pigen_lower_rtl_type(&lowering, t_signed);
		assert(!IS_INVALID_ID(id)); /* Deliberate red: stub returns the sentinel. */
		rt = pigen_rtl_type_get(&rtl, id);
		assert(rt);
		assert(rt->signedness == PIGEN_SIGN_SIGNED);
		assert(rt->signedness == pigen_data_type_signedness(&sem, t_signed));
		assert(rt->state_domain == pigen_data_type_state_domain(&sem, t_signed));
		assert(rt->width == 8);
		assert(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, t_signed), &w) && w == 8);
		/* Range shape: the owner's dimension count and packed element are
		 * preserved exactly on the lowered record. */
		assert(rt->dimension_count ==
			pigen_data_type_dimension_count(&sem, t_signed));
		assert(!IS_INVALID_ID(pigen_data_type_packed_element(&sem, t_signed)));

		/* Case 2: unsigned uint[12] preserves unsigned signedness, state domain,
		 * width 12 and the owner-reported range shape. */
		id = pigen_lower_rtl_type(&lowering, t_unsigned);
		assert(!IS_INVALID_ID(id));
		rt = pigen_rtl_type_get(&rtl, id);
		assert(rt);
		assert(rt->signedness == PIGEN_SIGN_UNSIGNED);
		assert(rt->signedness == pigen_data_type_signedness(&sem, t_unsigned));
		assert(rt->state_domain == pigen_data_type_state_domain(&sem, t_unsigned));
		assert(rt->width == 12);
		assert(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, t_unsigned), &w) && w == 12);
		assert(rt->dimension_count ==
			pigen_data_type_dimension_count(&sem, t_unsigned));
		assert(!IS_INVALID_ID(pigen_data_type_packed_element(&sem, t_unsigned)));

		/* Case 3: bit[16] preserves width 16, the state domain exactly as the
		 * owner reports it, and the owner's single packed range. */
		id = pigen_lower_rtl_type(&lowering, t_bit);
		assert(!IS_INVALID_ID(id));
		rt = pigen_rtl_type_get(&rtl, id);
		assert(rt);
		assert(rt->signedness == PIGEN_SIGN_UNSIGNED);
		assert(rt->signedness == pigen_data_type_signedness(&sem, t_bit));
		assert(rt->state_domain == pigen_data_type_state_domain(&sem, t_bit));
		assert(rt->width == 16);
		assert(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, t_bit), &w) && w == 16);
		/* bit[16] carries the owner's single packed range: the lowered record
		 * keeps that one dimension (the owner reports [15:0]). */
		assert(pigen_data_type_dimension_count(&sem, t_bit) == 1);
		assert(rt->dimension_count == 1 && rt->dimensions);
	}

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
	puts("PASS: rtl type lowering preserves owner state, width, signedness and range");
	return 0;
}
