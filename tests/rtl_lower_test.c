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
		size_t arena_before;
		size_t arena_after;

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
	return 0;
}
