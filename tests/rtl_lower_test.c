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
#include <string.h>

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

	/* (4) Expression lowering preserves the surviving width, signedness, every
	 * explicit conversion/projection and child order. Second expression-family
	 * section of the Task 5 test-contract chain. Staged red: the front-loaded
	 * invalid-expression-id sentinel assert below is the section's first assert
	 * and passes against the stub; the first deliberate red is case 1's
	 * `!IS_INVALID_ID(eid)`, since the stub still returns the sentinel for a
	 * valid constant expression. Cases 1-4 build real owner constant
	 * expressions through the owner APIs and assert only the returned
	 * pigen_rtl_expr record: its kind, the result type's signedness and width
	 * exactly as the owner's resolved result data type reports them, every
	 * explicit conversion and select kept on the record, and the lowered child
	 * count and order. Child order is asserted by identity: the owner's
	 * source children (as.binary.left/right, as.conversion.operand,
	 * as.select.base/left/right, as.sequence via pigen_const_expr_children)
	 * lowered by identity must be exactly the lowered children, in order -
	 * which a reordered or dropped-children implementation cannot shortcut.
	 * Case 5 pins the invalid-expression-id contract. */
	{
		pigen_data_type_id unsized;
		pigen_data_type_id t4;
		pigen_data_type_id t8;
		pigen_data_type_id t12;
		pigen_data_type_id t16;
		pigen_const_expr_id width8;
		pigen_const_expr_id a;
		pigen_const_expr_id b;
		pigen_const_expr_id base16;
		pigen_const_expr_id idx5;
		pigen_const_expr_id c4;
		pigen_const_expr_id c16;
		pigen_conversion conversion;
		pigen_data_type_id select_type;
		pigen_data_type_id concat_type;
		pigen_binary_resolution binary_resolution;
		pigen_const_expr_id add_expr;
		pigen_const_expr_id conv_expr;
		pigen_const_expr_id select_expr;
		pigen_const_expr_id concat_expr;
		const pigen_const_expr *owner_expr;
		const pigen_rtl_expr *re;
		const pigen_rtl_type *result_type;
		const pigen_rtl_expr_id *rtl_children;
		pigen_rtl_expr_id eid;
		size_t rtl_child_count;
		uint64_t w;
		size_t i;

		/* Owner data: signed int[8], uint[12], bit[16] and bit[4] plus the
		 * constant operands the cases build from, all through the owner APIs. */
		unsized = pigen_data_type_unsized_integer(&sem);
		assert(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem, 8, unsized);
		t8 = pigen_data_type_signed_integer(&sem, width8);
		t12 = pigen_data_type_unsigned_integer(&sem,
			pigen_const_expr_intern_integer(&sem, 12, unsized));
		t16 = pigen_data_type_sized_logic(&sem, 16, PIGEN_SIGN_UNSIGNED);
		t4 = pigen_data_type_sized_logic(&sem, 4, PIGEN_SIGN_UNSIGNED);
		assert(!IS_INVALID_ID(t8) && !IS_INVALID_ID(t12) &&
			!IS_INVALID_ID(t16) && !IS_INVALID_ID(t4));
		a = pigen_const_expr_intern_integer(&sem, 1, t8);
		b = pigen_const_expr_intern_integer(&sem, 2, t8);
		base16 = pigen_const_expr_intern_integer(&sem, 0x1234, t16);
		idx5 = pigen_const_expr_intern_integer(&sem, 5, unsized);
		c4 = pigen_const_expr_intern_integer(&sem, 0xAB, t4);
		c16 = pigen_const_expr_intern_integer(&sem, 0x5678, t16);
		assert(!IS_INVALID_ID(a) && !IS_INVALID_ID(b) &&
			!IS_INVALID_ID(base16) && !IS_INVALID_ID(idx5) &&
			!IS_INVALID_ID(c4) && !IS_INVALID_ID(c16));

		/* Case 5 (front-loaded, the section's first behavioral assert): an
		 * invalid constant-expression id returns the RTL-expression sentinel
		 * and leaves both identity memo maps untouched. */
		eid = pigen_lower_rtl_expression(&lowering, INVALID_EXPR);
		assert(IS_INVALID_ID(eid));
		assert(!lowering.lowered_types &&
			!lowering.lowered_type_count && !lowering.lowered_type_capacity);
		assert(!lowering.lowered_expressions &&
			!lowering.lowered_expression_count &&
			!lowering.lowered_expression_capacity);

		/* Case 1: widening binary arithmetic. signed int[8] + signed int[8]
		 * resolves through the owner to a signed result type strictly wider
		 * than the operands; the lowered record keeps the binary kind, the
		 * owner's result type's signedness and width, and both children in
		 * source child order (left then right). */
		assert(pigen_data_type_resolve_binary_operation(&sem, PIGEN_BINARY_ADD,
			t8, t8, &binary_resolution));
		add_expr = pigen_const_expr_intern_binary(&sem,
			binary_resolution.operation, a, b);
		owner_expr = pigen_const_expr_get(&sem, add_expr);
		assert(owner_expr && owner_expr->kind == PIGEN_CONST_EXPR_BINARY);
		assert(!IS_INVALID_ID(owner_expr->data_type));
		assert(pigen_data_type_signedness(&sem, owner_expr->data_type) ==
			PIGEN_SIGN_SIGNED);
		assert(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, owner_expr->data_type), &w));
		assert(w > 8); /* widening: the result is wider than the int[8] operands */
		eid = pigen_lower_rtl_expression(&lowering, add_expr);
		assert(!IS_INVALID_ID(eid)); /* Deliberate red: stub returns the sentinel. */
		re = pigen_rtl_expr_get(&rtl, eid);
		assert(re && re->kind == PIGEN_RTL_EXPR_BINARY);
		result_type = pigen_rtl_type_get(&rtl, re->type);
		assert(result_type);
		assert(result_type->signedness ==
			pigen_data_type_signedness(&sem, owner_expr->data_type));
		assert(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, owner_expr->data_type), &w));
		assert(result_type->width == w);
		/* Both children lowered, in source child order, by identity. */
		rtl_children = pigen_rtl_expr_children(&rtl, eid, &rtl_child_count);
		assert(rtl_children && rtl_child_count == 2);
		assert(rtl_children[0].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.binary.left).index);
		assert(rtl_children[1].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.binary.right).index);

		/* Case 2: explicit cast. An owner-resolved explicit conversion from
		 * bit[16] to signed int[12] is interned as a conversion constant;
		 * the lowered record keeps the conversion verbatim and the target
		 * type's signedness and width. */
		assert(pigen_data_type_resolve_explicit_conversion(&sem, t16, t8,
			&conversion));
		assert(pigen_data_type_conversion_is_valid(&sem, conversion));
		conv_expr = pigen_const_expr_intern_conversion(&sem, conversion, base16);
		owner_expr = pigen_const_expr_get(&sem, conv_expr);
		assert(owner_expr && owner_expr->kind == PIGEN_CONST_EXPR_CONVERSION);
		assert(!IS_INVALID_ID(owner_expr->data_type));
		assert(owner_expr->as.conversion.conversion.kind == conversion.kind);
		assert(owner_expr->as.conversion.conversion.source_data_type.index ==
			conversion.source_data_type.index);
		assert(owner_expr->as.conversion.conversion.target_data_type.index ==
			conversion.target_data_type.index);
		assert(pigen_data_type_signedness(&sem, owner_expr->data_type) ==
			PIGEN_SIGN_SIGNED);
		eid = pigen_lower_rtl_expression(&lowering, conv_expr);
		assert(!IS_INVALID_ID(eid));
		re = pigen_rtl_expr_get(&rtl, eid);
		assert(re && re->kind == PIGEN_RTL_EXPR_CONVERSION);
		assert(re->as.conversion.conversion.kind == conversion.kind);
		assert(re->as.conversion.conversion.source_data_type.index ==
			conversion.source_data_type.index);
		assert(re->as.conversion.conversion.target_data_type.index ==
			conversion.target_data_type.index);
		result_type = pigen_rtl_type_get(&rtl, re->type);
		assert(result_type);
		assert(result_type->signedness ==
			pigen_data_type_signedness(&sem, owner_expr->data_type));
		assert(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, owner_expr->data_type), &w));
		assert(result_type->width == w);
		/* The single operand is lowered and kept, by identity. */
		rtl_children = pigen_rtl_expr_children(&rtl, eid, &rtl_child_count);
		assert(rtl_children && rtl_child_count == 1);
		assert(rtl_children[0].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.conversion.operand).index);

		/* Case 3: projection (select). A constant bit[15:0] range-selected to
		 * bits [5:0] through the owner keeps the select kind and the
		 * owner's projected width on the lowered record. */
		select_type = pigen_data_type_packed_select(&sem, t16, idx5,
			pigen_const_expr_intern_integer(&sem, 0, unsized),
			PIGEN_SEMANTIC_SELECT_RANGE);
		assert(!IS_INVALID_ID(select_type));
		assert(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, select_type), &w) && w == 6);
		select_expr = pigen_const_expr_intern_select(&sem, base16, idx5,
			pigen_const_expr_intern_integer(&sem, 0, unsized),
			PIGEN_SEMANTIC_SELECT_RANGE, select_type);
		owner_expr = pigen_const_expr_get(&sem, select_expr);
		assert(owner_expr && owner_expr->kind == PIGEN_CONST_EXPR_SELECT);
		assert(!IS_INVALID_ID(owner_expr->data_type));
		assert(owner_expr->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE);
		eid = pigen_lower_rtl_expression(&lowering, select_expr);
		assert(!IS_INVALID_ID(eid));
		re = pigen_rtl_expr_get(&rtl, eid);
		assert(re && re->kind == PIGEN_RTL_EXPR_SELECT);
		assert(re->as.select.kind == owner_expr->as.select.kind);
		result_type = pigen_rtl_type_get(&rtl, re->type);
		assert(result_type);
		assert(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, owner_expr->data_type), &w));
		assert(result_type->width == w);
		/* Base and range bounds lowered, in owner child order (base, left,
		 * right), by identity. */
		rtl_children = pigen_rtl_expr_children(&rtl, eid, &rtl_child_count);
		assert(rtl_children && rtl_child_count == 3);
		assert(rtl_children[0].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.select.base).index);
		assert(rtl_children[1].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.select.left).index);
		assert(rtl_children[2].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.select.right).index);

		/* Case 4: concatenation. A constant bit[4] concatenated with a
		 * constant bit[16] keeps the owner's combined width and both
		 * children in source order. */
		{
			pigen_data_type_id concat_types[2];
			const pigen_const_expr_id *seq_owner_children;

			concat_types[0] = t4;
			concat_types[1] = t16;
			concat_type = pigen_data_type_concatenation(&sem, concat_types, 2);
			assert(!IS_INVALID_ID(concat_type));
			assert(pigen_const_expr_evaluate_u64(&sem,
				pigen_data_type_packed_width(&sem, concat_type), &w) &&
				w == 20);
			{
				pigen_const_expr_id concat_children[2];

				concat_children[0] = c4;
				concat_children[1] = c16;
				concat_expr = pigen_const_expr_intern_concatenation(&sem,
					concat_children, 2, concat_type);
			}
			owner_expr = pigen_const_expr_get(&sem, concat_expr);
			assert(owner_expr &&
				owner_expr->kind == PIGEN_CONST_EXPR_CONCATENATION);
			assert(!IS_INVALID_ID(owner_expr->data_type));
			eid = pigen_lower_rtl_expression(&lowering, concat_expr);
			assert(!IS_INVALID_ID(eid));
			re = pigen_rtl_expr_get(&rtl, eid);
			assert(re && re->kind == PIGEN_RTL_EXPR_CONCATENATION);
			result_type = pigen_rtl_type_get(&rtl, re->type);
			assert(result_type);
			assert(pigen_const_expr_evaluate_u64(&sem,
				pigen_data_type_packed_width(&sem, owner_expr->data_type),
				&w));
			assert(result_type->width == w);
			/* Owner's children via the children arena, in source order. */
			seq_owner_children = pigen_const_expr_children(&sem,
				owner_expr->as.sequence.first_child,
				owner_expr->as.sequence.child_count);
			assert(seq_owner_children &&
				owner_expr->as.sequence.child_count == 2);
			assert(seq_owner_children[0].index == c4.index);
			assert(seq_owner_children[1].index == c16.index);
			rtl_children = pigen_rtl_expr_children(&rtl, eid, &rtl_child_count);
			assert(rtl_children && rtl_child_count == 2);
			for (i = 0; i < rtl_child_count; i++)
				assert(rtl_children[i].index ==
					pigen_lower_rtl_expression(&lowering,
						seq_owner_children[i]).index);
		}
	}

	/* (5) Constants are lowered once by identity: the same source constant
	 * expression lowered twice returns the same memoized RTL handle and does
	 * not grow the RTL expression arena, a distinct constant of the same type
	 * gets a different handle, and the identity memo slot is stable. Third
	 * expression-family section of the Task 5 test-contract chain (the
	 * constant-identity family). Staged red: the front-loaded
	 * invalid-constant sentinel assert below is the section's first assert and
	 * passes against the stub; the first deliberate red is case 1's
	 * `!IS_INVALID_ID(eid)`, since the stub still returns the sentinel for a
	 * valid constant expression. Cases 1-3 build real owner constant
	 * expressions through the owner APIs and assert only the returned
	 * pigen_rtl_expr_id and the public RTL expression-arena count - never an
	 * imagined internal memo representation - so the implementer cannot
	 * shortcut the once-by-identity contract. Case 4 pins the
	 * invalid-constant-expression contract. */
	{
		pigen_data_type_id unsized;
		pigen_const_expr_id width16;
		pigen_data_type_id t16;
		pigen_const_expr_id c5;
		pigen_const_expr_id c7;
		pigen_rtl_expr_id eid;
		pigen_rtl_expr_id eid_again;
		pigen_rtl_expr_id eid_distinct;
		const pigen_rtl_expr *re;
		size_t arena_before;
		size_t arena_after;
		uint64_t w;

		/* Owner data: a 16-bit unsigned type and two distinct constants of it,
		 * all through the owner APIs. The two values differ so the owner
		 * interning keeps them at distinct arena indices; identity is by arena
		 * index, never by value. */
		unsized = pigen_data_type_unsized_integer(&sem);
		assert(!IS_INVALID_ID(unsized));
		width16 = pigen_const_expr_intern_integer(&sem, 16, unsized);
		t16 = pigen_data_type_unsigned_integer(&sem, width16);
		assert(!IS_INVALID_ID(width16) && !IS_INVALID_ID(t16));
		c5 = pigen_const_expr_intern_integer(&sem, 5, t16);
		c7 = pigen_const_expr_intern_integer(&sem, 7, t16);
		assert(!IS_INVALID_ID(c5) && !IS_INVALID_ID(c7));
		assert(c5.index != c7.index); /* distinct arena indices, same type */
		/* Same value re-interned is deduplicated by the owner to the same arena
		 * index, so it is not a distinct constant. */
		assert(c5.index ==
			pigen_const_expr_intern_integer(&sem, 5, t16).index);

		/* Case 4 (front-loaded, the section's first behavioral assert): an
		 * invalid constant-expression id returns the RTL-expression sentinel and
		 * leaves the identity memo maps untouched. */
		eid = pigen_lower_rtl_expression(&lowering, INVALID_EXPR);
		assert(IS_INVALID_ID(eid));
		assert(!lowering.lowered_expressions &&
			!lowering.lowered_expression_count &&
			!lowering.lowered_expression_capacity);

		/* Case 1: lower the SAME constant (c5) twice. Both calls return the
		 * same valid memoized RTL handle and the second call does not grow the
		 * RTL expression arena. */
		eid = pigen_lower_rtl_expression(&lowering, c5);
		assert(!IS_INVALID_ID(eid)); /* Deliberate red: stub returns the sentinel. */
		arena_before = rtl.expression_count;
		eid_again = pigen_lower_rtl_expression(&lowering, c5);
		arena_after = rtl.expression_count;
		assert(eid_again.index == eid.index); /* memoized: same handle */
		assert(arena_after == arena_before); /* no arena growth on the second call */

		/* Case 2: a distinct constant of the same type (c7) gets a DIFFERENT
		 * valid RTL handle - identity is by arena index, not by value or type. */
		eid_distinct = pigen_lower_rtl_expression(&lowering, c7);
		assert(!IS_INVALID_ID(eid_distinct));
		assert(eid_distinct.index != eid.index);

		/* Case 3: the identity memo slot is stable: the slot keyed by the
		 * constant's arena index equals the returned handle. */
		assert(lowering.lowered_expression_count > c5.index);
		assert(lowering.lowered_expressions[c5.index].index == eid.index);

		/* Content: the ALREADY-LOWERED bare integer constants publish the
		 * correct record content, not just a handle. This closes the
		 * constant-identity CONTENT gap: an implementation that memoizes by
		 * identity but publishes the wrong literal value, the wrong kind (e.g.
		 * PIGEN_RTL_EXPR_BITS or PIGEN_RTL_EXPR_OBJECT) or a wrong/missing type
		 * would otherwise pass the whole section. Only the returned
		 * pigen_rtl_expr record and owner-reported facts are asserted - never
		 * an imagined internal memo representation. */

		/* c5 (the lowered value 5) is a PIGEN_RTL_EXPR_INTEGER carrying the
		 * owner-reported value and the lowered id of its owner type t16. */
		re = pigen_rtl_expr_get(&rtl, eid);
		assert(re);
		assert(re->kind == PIGEN_RTL_EXPR_INTEGER);
		assert(pigen_const_expr_evaluate_u64(&sem, c5, &w));
		assert(re->value == w); /* owner-reported value (5) on the record */
		assert(re->type.index ==
			pigen_lower_rtl_type(&lowering, t16).index);

		/* c7 (the lowered value 7, the distinct constant) carries the same
		 * integer kind, its own owner-reported value and the same lowered
		 * t16 type. */
		re = pigen_rtl_expr_get(&rtl, eid_distinct);
		assert(re);
		assert(re->kind == PIGEN_RTL_EXPR_INTEGER);
		assert(pigen_const_expr_evaluate_u64(&sem, c7, &w));
		assert(re->value == w); /* owner-reported value (7) on the record */
		assert(re->type.index ==
			pigen_lower_rtl_type(&lowering, t16).index);
	}

	/* (6) Error rollback: an invalid expression and an unbound signal each
	 * report the error with NO partial RTL records published. Fourth
	 * expression-family section of the Task 5 test-contract chain (the
	 * error-rollback family). Cases build one real validated model through the
	 * owner APIs (a source file, a compilation scope, a module, and a declared
	 * signal), then drive the two failure modes and assert only observable
	 * state:
	 *   Case 1: a constant expression referencing the declared signal lowers to
	 *     the RTL-expression sentinel.
	 *   Case 2: after Case 1 the RTL type/expression arena counts and every
	 *     previously populated identity-memo slot are exactly unchanged - the
	 *     failed call published no partial RTL records.
	 *   Case 3: a constant-expression id that does not exist in the semantic
	 *     arena lowers to the sentinel and again leaves every count and slot
	 *     unchanged.
	 *   Case 4: after both failures an already-lowered valid constant still
	 *     returns its memoized valid RTL handle - the error left the lowering
	 *     usable (recoverable).
	 * Staged red: Cases 1-3 pass against the stub, which returns the sentinel
	 * for every input and publishes nothing - the sentinel guard and the
	 * no-partial-state comparison both hold under the stub. The FIRST deliberate
	 * red assert in this section is Case 4's `!IS_INVALID_ID(recoverable)`,
	 * marked below: it is the only assert that requires implemented behavior,
	 * because only a real lowering memoizes a valid constant so that the
	 * recoverable call returns a non-sentinel handle while the stub returns the
	 * sentinel. Cases 1-3 are guards that pin the error-reporting contract. */
	{
		const char *text =
			"module top : input value : logic ;\n";
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id signal_symbol;
		pigen_module_id module;
		pigen_signal_id signal;
		pigen_data_type_id t16;
		pigen_const_expr_id width16;
		pigen_const_expr_id good;
		pigen_const_expr_id sym_expr;
		pigen_rtl_expr_id good_id;
		pigen_rtl_expr_id eid;
		pigen_rtl_expr_id recoverable;
		size_t type_count_before, type_count_after;
		size_t expr_count_before, expr_count_after;
		size_t type_map_before, type_map_after;
		size_t expr_map_before, expr_map_after;
		size_t i;
		int no_partial_state;

		/* Build the signal's 16-bit type first, then the real validated owner
		 * model: a source file, the compilation scope, a module in it, and one
		 * declared signal of that type. The spans below are chosen so every
		 * owner span constraint holds: the module symbol's declaration spans
		 * the whole file (as module_add requires), the module scope uses the
		 * same span, and the signal symbol's name span is contained in and its
		 * declaration span equal to the span signal_add checks. */
		width16 = pigen_const_expr_intern_integer(&sem, 16,
			pigen_data_type_unsized_integer(&sem));
		assert(!IS_INVALID_ID(width16));
		t16 = pigen_data_type_unsigned_integer(&sem, width16);
		assert(!IS_INVALID_ID(t16));
		source = pigen_source_add(&sources, "lower_rollback.pigen", text,
			strlen(text));
		assert(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name = (pigen_source_span){source, 19, 24}; /* "value" */
		sem.compilation_scope = pigen_scope_add(&sem,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		assert(sem.compilation_scope.index != PIGEN_INVALID_ID);
		assert(pigen_symbol_declare(&sem, sem.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem, sem.compilation_scope, whole);
		assert(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		assert(module.index != PIGEN_INVALID_ID);
		assert(pigen_symbol_declare(&sem, module_scope, PIGEN_SYMBOL_SIGNAL,
			t16, name, name, &signal_symbol, NULL) == PIGEN_DECLARE_OK);
		signal = pigen_signal_add(&sem, (pigen_syntax_id){2}, module,
			signal_symbol, t16, pigen_semantic_scalar_shape(&sem),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
			PIGEN_SEMANTIC_INTERNAL, name);
		assert(signal.index != PIGEN_INVALID_ID);

		/* A valid constant of the same type, lowered before any failure, is the
		 * recoverability witness for Case 4. */
		good = pigen_const_expr_intern_integer(&sem, 5, t16);
		assert(!IS_INVALID_ID(good));
		good_id = pigen_lower_rtl_expression(&lowering, good);
		assert(!IS_INVALID_ID(good_id)); /* Deliberate red (first in this
		 * section): the stub returns the sentinel for the valid constant. */

		/* Case 1: a constant expression referencing the declared signal lowers
		 * to the sentinel (a guard: the stub also returns the sentinel). */
		sym_expr = pigen_const_expr_intern_symbol(&sem, signal_symbol, t16);
		assert(!IS_INVALID_ID(sym_expr));
		eid = pigen_lower_rtl_expression(&lowering, sym_expr);
		assert(IS_INVALID_ID(eid));

		/* Case 2: the failure published no partial RTL records - the arena
		 * counts and every previously populated memo slot are unchanged. */
		type_count_before = rtl.type_count;
		expr_count_before = rtl.expression_count;
		type_map_before = lowering.lowered_type_count;
		expr_map_before = lowering.lowered_expression_count;
		type_count_after = rtl.type_count;
		expr_count_after = rtl.expression_count;
		type_map_after = lowering.lowered_type_count;
		expr_map_after = lowering.lowered_expression_count;
		no_partial_state = type_count_after == type_count_before &&
			expr_count_after == expr_count_before &&
			type_map_after == type_map_before &&
			expr_map_after == expr_map_before;
		for (i = 0; i < type_map_after; i++)
			no_partial_state = no_partial_state &&
				lowering.lowered_types[i].index == PIGEN_INVALID_ID;
		for (i = 0; i < expr_map_after; i++)
			no_partial_state = no_partial_state &&
				(i == good.index ?
					lowering.lowered_expressions[i].index == good_id.index :
					lowering.lowered_expressions[i].index == PIGEN_INVALID_ID);
		assert(no_partial_state); /* guard: the stub publishes nothing */

		/* Case 3: a constant-expression id absent from the semantic arena
		 * lowers to the sentinel and again leaves every count and slot
		 * unchanged. */
		eid = pigen_lower_rtl_expression(&lowering,
			(pigen_const_expr_id){999});
		assert(IS_INVALID_ID(eid));
		type_count_after = rtl.type_count;
		expr_count_after = rtl.expression_count;
		type_map_after = lowering.lowered_type_count;
		expr_map_after = lowering.lowered_expression_count;
		no_partial_state = type_count_after == type_count_before &&
			expr_count_after == expr_count_before &&
			type_map_after == type_map_before &&
			expr_map_after == expr_map_before;
		for (i = 0; i < type_map_after; i++)
			no_partial_state = no_partial_state &&
				lowering.lowered_types[i].index == PIGEN_INVALID_ID;
		for (i = 0; i < expr_map_after; i++)
			no_partial_state = no_partial_state &&
				(i == good.index ?
					lowering.lowered_expressions[i].index == good_id.index :
					lowering.lowered_expressions[i].index == PIGEN_INVALID_ID);
		assert(no_partial_state); /* guard: the stub publishes nothing */

		/* Case 4: after both failures the already-lowered valid constant still
		 * returns its memoized valid handle - the error left the lowering
		 * usable. */
		recoverable = pigen_lower_rtl_expression(&lowering, good);
		assert(!IS_INVALID_ID(recoverable)); /* Deliberate red (first in this
		 * section): the stub returns the sentinel for the valid constant. */
		assert(recoverable.index == good_id.index);
	}

	/* (7) Repeat lowering is identity-memoized and deterministic. Fifth
	 * expression-family section of the Task 5 test-contract chain (the
	 * memo-stability and determinism family). Cases build one real
	 * pigen_semantic_model through the owner APIs (an 8-bit signed type, a
	 * 16-bit unsigned type, and three constants including one explicit
	 * conversion) and drive the lowering contract from outside:
	 *   Case 1: REPEAT-LOWER STABILITY. Lower every identity in the fixed set
	 *     and record every returned id; lower the WHOLE set again and assert
	 *     every returned id is identical to the first pass, the memo map
	 *     counts (lowered_type_count, lowered_expression_count) and the RTL
	 *     type/expression arena counts are unchanged - no duplicate growth.
	 *   Case 2: INDEPENDENT-BUILD DETERMINISM. Build a SECOND
	 *     pigen_semantic_model + pigen_rtl_model with the IDENTICAL
	 *     construction sequence (same API calls, same values, fresh managers)
	 *     and run the identical lowering sequence through a second
	 *     pigen_rtl_lowering; for every lowered identity the lowered
	 *     pigen_rtl_type / pigen_rtl_expr record fields (kind, type, width,
	 *     signedness, state domain, child count and child ids relative to the
	 *     arena, literal fields) are byte-identical to the first build's
	 *     records - ids may differ only by arena offset, so record contents,
	 *     not raw ids, are compared. The second build frees its own models
	 *     before the shared tail.
	 *   Case 3: MEMO MAP COHERENCE. After both passes, every populated
	 *     lowering.lowered_types[i] / lowering.lowered_expressions[i] slot
	 *     equals the id pigen_rtl_type_get / pigen_rtl_expr_get can resolve
	 *     in its RTL model (no dangling slots).
	 * Staged red: every owner-construction assert passes against the landed
	 * owners; the FIRST deliberate unimplemented-behavior assert in this
	 * section is Case 1's first `!IS_INVALID_ID(id1)` (second pass, t8s),
	 * marked below - the stub returns the sentinel for every valid identity,
	 * so the second pass's valid-id check fails first. Every later assert in
	 * this section requires implemented behavior and is also deliberate red.
	 * This is the LAST test-contract section of the chain: the implementation
	 * item makes the whole target green. */
	{
		pigen_data_type_id unsized;
		pigen_data_type_id t8s;
		pigen_data_type_id t16u;
		pigen_const_expr_id width8;
		pigen_const_expr_id width16;
		pigen_const_expr_id c5;
		pigen_const_expr_id c0x1234;
		pigen_const_expr_id conv;
		pigen_conversion conversion;
		pigen_rtl_type_id p1_t8s;
		pigen_rtl_type_id p1_t16u;
		pigen_rtl_expr_id p1_c5;
		pigen_rtl_expr_id p1_c16;
		pigen_rtl_expr_id p1_conv;
		pigen_rtl_type_id id1;
		const pigen_rtl_type *rt;
		const pigen_rtl_expr *re;
		size_t type_count_before;
		size_t expr_count_before;
		size_t type_map_before;
		size_t expr_map_before;
		size_t type_count_after;
		size_t expr_count_after;
		size_t type_map_after;
		size_t expr_map_after;
		size_t i;
		int stable;

		/* Owner data for the fixed set: one 8-bit signed type, one 16-bit
		 * unsigned type and three constants (two integers and one explicit
		 * conversion), all through the owner APIs. */
		unsized = pigen_data_type_unsized_integer(&sem);
		assert(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem, 8, unsized);
		width16 = pigen_const_expr_intern_integer(&sem, 16, unsized);
		assert(!IS_INVALID_ID(width8) && !IS_INVALID_ID(width16));
		t8s = pigen_data_type_signed_integer(&sem, width8);
		t16u = pigen_data_type_unsigned_integer(&sem, width16);
		assert(!IS_INVALID_ID(t8s) && !IS_INVALID_ID(t16u));
		c5 = pigen_const_expr_intern_integer(&sem, 5, t8s);
		c0x1234 = pigen_const_expr_intern_integer(&sem, 0x1234, t16u);
		assert(!IS_INVALID_ID(c5) && !IS_INVALID_ID(c0x1234));
		assert(pigen_data_type_resolve_explicit_conversion(&sem, t16u, t8s,
			&conversion));
		assert(pigen_data_type_conversion_is_valid(&sem, conversion));
		conv = pigen_const_expr_intern_conversion(&sem, conversion, c0x1234);
		assert(!IS_INVALID_ID(conv));

		/* Case 1: REPEAT-LOWER STABILITY. Lower the whole fixed set (types,
		 * then constants) and record every returned id, then lower the whole
		 * set AGAIN and assert every returned id is identical, the memo map
		 * counts and the RTL arena counts are unchanged - no duplicate growth.
		 * The second pass's valid-id checks are this section's first
		 * deliberate red asserts. */
		/* First pass: lower the whole fixed set and record every returned id.
		 * The stub returns the sentinel for each, so no id is asserted valid
		 * yet - the first deliberate red is the second pass below. */
		p1_t8s = pigen_lower_rtl_type(&lowering, t8s);
		p1_t16u = pigen_lower_rtl_type(&lowering, t16u);
		p1_c5 = pigen_lower_rtl_expression(&lowering, c5);
		p1_c16 = pigen_lower_rtl_expression(&lowering, c0x1234);
		p1_conv = pigen_lower_rtl_expression(&lowering, conv);
		type_count_before = rtl.type_count;
		expr_count_before = rtl.expression_count;
		type_map_before = lowering.lowered_type_count;
		expr_map_before = lowering.lowered_expression_count;

		/* Second pass over the WHOLE set: every returned id is identical to
		 * the first pass and nothing grows. */
		id1 = pigen_lower_rtl_type(&lowering, t8s);
		assert(!IS_INVALID_ID(id1)); /* Deliberate red (first in this
		 * section): the stub returns the sentinel for a valid type. */
		assert(id1.index == p1_t8s.index);
		assert(pigen_lower_rtl_type(&lowering, t16u).index == p1_t16u.index);
		assert(pigen_lower_rtl_expression(&lowering, c5).index == p1_c5.index);
		assert(pigen_lower_rtl_expression(&lowering, c0x1234).index ==
			p1_c16.index);
		assert(pigen_lower_rtl_expression(&lowering, conv).index ==
			p1_conv.index);
		type_count_after = rtl.type_count;
		expr_count_after = rtl.expression_count;
		type_map_after = lowering.lowered_type_count;
		expr_map_after = lowering.lowered_expression_count;
		stable = type_count_after == type_count_before &&
			expr_count_after == expr_count_before &&
			type_map_after == type_map_before &&
			expr_map_after == expr_map_before;
		assert(stable); /* memoized: no duplicate growth on the repeat pass */

		/* Case 2: INDEPENDENT-BUILD DETERMINISM. Build a second model pair
		 * with the IDENTICAL construction sequence (same API calls, same
		 * values, fresh managers), run the identical lowering sequence through
		 * a second pigen_rtl_lowering, and compare every lowered record
		 * field by field against the first build's records. Ids may differ
		 * only by arena offset, so record contents are compared, not raw ids:
		 * for types the record fields (signedness, state domain, width,
		 * dimension count and per-dimension bounds); for expressions kind,
		 * the low word value, the literal fields, the child count, and - for
		 * the conversion - the conversion kept verbatim plus its single
		 * operand's child id RELATIVE TO THE ARENA (offset from the build's
		 * first lowered child). The second build frees its own models before
		 * the shared tail. */
		{
			pigen_source_manager sources2 = {0};
			pigen_semantic_model sem2;
			pigen_rtl_model rtl2 = {0};
			pigen_rtl_lowering lowering2;
			pigen_data_type_id unsized2;
			pigen_data_type_id t8s2;
			pigen_data_type_id t16u2;
			pigen_const_expr_id width8_2;
			pigen_const_expr_id width16_2;
			pigen_const_expr_id c5_2;
			pigen_const_expr_id c0x1234_2;
			pigen_const_expr_id conv_2;
			pigen_conversion conversion2;
			const pigen_rtl_type *rt2;
			const pigen_rtl_expr *re2;
			const pigen_rtl_expr_id *kids1;
			const pigen_rtl_expr_id *kids2;
			size_t kids1_count;
			size_t kids2_count;
			uint64_t words;
			int deterministic;

			pigen_semantic_init(&sem2, &sources2);
			pigen_rtl_lowering_init(&lowering2, &sem2, &rtl2);

			/* Identical construction sequence, fresh managers. */
			unsized2 = pigen_data_type_unsized_integer(&sem2);
			assert(!IS_INVALID_ID(unsized2));
			width8_2 = pigen_const_expr_intern_integer(&sem2, 8, unsized2);
			width16_2 = pigen_const_expr_intern_integer(&sem2, 16, unsized2);
			assert(!IS_INVALID_ID(width8_2) && !IS_INVALID_ID(width16_2));
			t8s2 = pigen_data_type_signed_integer(&sem2, width8_2);
			t16u2 = pigen_data_type_unsigned_integer(&sem2, width16_2);
			assert(!IS_INVALID_ID(t8s2) && !IS_INVALID_ID(t16u2));
			c5_2 = pigen_const_expr_intern_integer(&sem2, 5, t8s2);
			c0x1234_2 = pigen_const_expr_intern_integer(&sem2, 0x1234, t16u2);
			assert(!IS_INVALID_ID(c5_2) && !IS_INVALID_ID(c0x1234_2));
			assert(pigen_data_type_resolve_explicit_conversion(&sem2, t16u2,
				t8s2, &conversion2));
			assert(pigen_data_type_conversion_is_valid(&sem2, conversion2));
			conv_2 = pigen_const_expr_intern_conversion(&sem2, conversion2,
				c0x1234_2);
			assert(!IS_INVALID_ID(conv_2));

			/* Identical lowering sequence through the second lowering. */
			pigen_lower_rtl_type(&lowering2, t8s2);
			pigen_lower_rtl_type(&lowering2, t16u2);
			pigen_lower_rtl_expression(&lowering2, c5_2);
			pigen_lower_rtl_expression(&lowering2, c0x1234_2);
			pigen_lower_rtl_expression(&lowering2, conv_2);

			/* Types: record contents are byte-identical between the builds. */
			rt = pigen_rtl_type_get(&rtl, pigen_lower_rtl_type(&lowering, t8s));
			rt2 = pigen_rtl_type_get(&rtl2, pigen_lower_rtl_type(&lowering2,
				t8s2));
			deterministic = rt && rt2 &&
				rt->signedness == rt2->signedness &&
				rt->state_domain == rt2->state_domain &&
				rt->width == rt2->width &&
				rt->dimension_count == rt2->dimension_count;
			for (i = 0; deterministic && i < rt->dimension_count; i++) {
				const pigen_rtl_packed_dimension *d1;
				const pigen_rtl_packed_dimension *d2;

				d1 = &rt->dimensions[i];
				d2 = &rt2->dimensions[i];
				deterministic =
					d1->left.value == d2->left.value &&
					d1->right.value == d2->right.value &&
					d1->left.expression.index == d2->left.expression.index &&
					d1->right.expression.index ==
					d2->right.expression.index;
			}
			rt = pigen_rtl_type_get(&rtl, pigen_lower_rtl_type(&lowering, t16u));
			rt2 = pigen_rtl_type_get(&rtl2, pigen_lower_rtl_type(&lowering2,
				t16u2));
			deterministic = deterministic && rt && rt2 &&
				rt->signedness == rt2->signedness &&
				rt->state_domain == rt2->state_domain &&
				rt->width == rt2->width &&
				rt->dimension_count == rt2->dimension_count;
			for (i = 0; deterministic && i < rt->dimension_count; i++) {
				const pigen_rtl_packed_dimension *d1;
				const pigen_rtl_packed_dimension *d2;

				d1 = &rt->dimensions[i];
				d2 = &rt2->dimensions[i];
				deterministic =
					d1->left.value == d2->left.value &&
					d1->right.value == d2->right.value &&
					d1->left.expression.index == d2->left.expression.index &&
					d1->right.expression.index ==
					d2->right.expression.index;
			}
			assert(deterministic); /* Deliberate red: stubs publish no records. */

			/* The c5 constant: kind, low word, literal fields, child count
			 * and the lowered record's own type record contents. */
			re = pigen_rtl_expr_get(&rtl,
				pigen_lower_rtl_expression(&lowering, c5));
			re2 = pigen_rtl_expr_get(&rtl2,
				pigen_lower_rtl_expression(&lowering2, c5_2));
			deterministic = re && re2 &&
				re->kind == re2->kind &&
				re->value == re2->value &&
				re->literal_bit_count == re2->literal_bit_count &&
				re->literal_negative == re2->literal_negative &&
				re->child_count == re2->child_count;
			if (deterministic && re->literal_bit_count) {
				words = (re->literal_bit_count + 63) / 64;
				for (i = 0; i < words; i++) {
					const pigen_rtl_literal_word *w1;
					const pigen_rtl_literal_word *w2;

					w1 = &re->literal_words[i];
					w2 = &re2->literal_words[i];
					deterministic = w1->value == w2->value &&
						w1->x_mask == w2->x_mask &&
						w1->z_mask == w2->z_mask;
				}
			}
			if (deterministic) {
				rt = pigen_rtl_type_get(&rtl, re->type);
				rt2 = pigen_rtl_type_get(&rtl2, re2->type);
				deterministic = rt && rt2 &&
					rt->signedness == rt2->signedness &&
					rt->state_domain == rt2->state_domain &&
					rt->width == rt2->width;
			}
			assert(deterministic); /* Deliberate red: stubs publish no records. */

			/* The 0x1234 constant: same record-content comparison. */
			re = pigen_rtl_expr_get(&rtl,
				pigen_lower_rtl_expression(&lowering, c0x1234));
			re2 = pigen_rtl_expr_get(&rtl2,
				pigen_lower_rtl_expression(&lowering2, c0x1234_2));
			deterministic = re && re2 &&
				re->kind == re2->kind &&
				re->value == re2->value &&
				re->literal_bit_count == re2->literal_bit_count &&
				re->literal_negative == re2->literal_negative &&
				re->child_count == re2->child_count;
			if (deterministic && re->literal_bit_count) {
				words = (re->literal_bit_count + 63) / 64;
				for (i = 0; i < words; i++) {
					const pigen_rtl_literal_word *w1;
					const pigen_rtl_literal_word *w2;

					w1 = &re->literal_words[i];
					w2 = &re2->literal_words[i];
					deterministic = w1->value == w2->value &&
						w1->x_mask == w2->x_mask &&
						w1->z_mask == w2->z_mask;
				}
			}
			if (deterministic) {
				rt = pigen_rtl_type_get(&rtl, re->type);
				rt2 = pigen_rtl_type_get(&rtl2, re2->type);
				deterministic = rt && rt2 &&
					rt->signedness == rt2->signedness &&
					rt->state_domain == rt2->state_domain &&
					rt->width == rt2->width;
			}
			assert(deterministic); /* Deliberate red: stubs publish no records. */

			/* The conversion: kind, the conversion kept verbatim, and the
			 * single operand's child id relative to the arena: the offset
			 * from the conversion record to its operand child is the same in
			 * both arenas (absolute child ids may differ by arena offset). */
			re = pigen_rtl_expr_get(&rtl,
				pigen_lower_rtl_expression(&lowering, conv));
			re2 = pigen_rtl_expr_get(&rtl2,
				pigen_lower_rtl_expression(&lowering2, conv_2));
			deterministic = re && re2 &&
				re->kind == re2->kind &&
				re->as.conversion.conversion.kind ==
				re2->as.conversion.conversion.kind &&
				re->child_count == re2->child_count;
			if (deterministic) {
				size_t base1;
				size_t base2;

				kids1 = pigen_rtl_expr_children(&rtl,
					pigen_lower_rtl_expression(&lowering, conv),
					&kids1_count);
				kids2 = pigen_rtl_expr_children(&rtl2,
					pigen_lower_rtl_expression(&lowering2, conv_2),
					&kids2_count);
				deterministic = kids1 && kids2 &&
					kids1_count == kids2_count &&
					kids1_count == re->child_count &&
					kids2_count == re2->child_count;
				if (deterministic && kids1_count) {
					base1 = pigen_lower_rtl_expression(&lowering, conv).index;
					base2 = pigen_lower_rtl_expression(&lowering2,
						conv_2).index;
					deterministic =
						kids1[0].index >= base1 &&
						kids2[0].index >= base2 &&
						(kids1[0].index - base1) ==
						(kids2[0].index - base2);
				}
			}
			assert(deterministic); /* Deliberate red: stubs publish no records. */

			/* The second build owns its models: free them before the shared
			 * tail so the shared tail frees only the first build. */
			pigen_rtl_lowering_free(&lowering2);
			pigen_free_rtl_model(&rtl2);
			pigen_free_semantic_model(&sem2);
			pigen_free_sources(&sources2);
		}

		/* Case 3: MEMO MAP COHERENCE. After both passes, every populated
		 * lowering.lowered_types[i] / lowering.lowered_expressions[i] slot
		 * resolves in the RTL model (no dangling slots). */
		for (i = 0; i < lowering.lowered_type_count; i++) {
			if (lowering.lowered_types[i].index == PIGEN_INVALID_ID)
				continue;
			rt = pigen_rtl_type_get(&rtl, lowering.lowered_types[i]);
			assert(rt); /* Deliberate red: stubs populate no map slots. */
		}
		for (i = 0; i < lowering.lowered_expression_count; i++) {
			if (lowering.lowered_expressions[i].index == PIGEN_INVALID_ID)
				continue;
			re = pigen_rtl_expr_get(&rtl, lowering.lowered_expressions[i]);
			assert(re); /* Deliberate red: stubs populate no map slots. */
		}
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
	puts("PASS: rtl expression lowering preserves width, signedness, conversions, projections and child order");
	puts("PASS: rtl constant lowering is once by identity");
	puts("PASS: rtl lowering errors publish no partial records and stay recoverable");
	puts("PASS: rtl lowering is identity-memoized, stable and deterministic");
	return 0;
}
