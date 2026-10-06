/* Task 5 plus Task 6 lowering contract for owner-based type/expression
 * lowering, fully implemented against the landed owner APIs.
 *
 * It gates the landed interface:
 *   pigen_rtl_lowering { semantics, rtl, lowered-type map, lowered-expr map }
 *   pigen_rtl_lowering_init(lowering, semantics, rtl)
 *   pigen_rtl_lowering_free(lowering)
 *   pigen_lower_rtl_type(lowering, type)
 *   pigen_lower_rtl_expression(lowering, expression)
 *
 * Every section is green against the landed lowering: init stores the models
 * and leaves both identity memo maps empty, and each lowering entry point
 * publishes a validated source identity as an RTL record with its
 * width/signedness/child-order propagation, its conversion and projection
 * handling, and the sentinel behavior that must never regress; this driver
 * asserts each lowered record against owner-reported facts and that sentinel. */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/rtl_lower.h"
#include "pigen/predicate.h"
#include "pigen/semantic.h"
#include "pigen/source.h"

#define INVALID_TYPE ((pigen_data_type_id){PIGEN_INVALID_ID})
#define INVALID_EXPR ((pigen_const_expr_id){PIGEN_INVALID_ID})
#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)

/* Collect every expression identity reachable from one RTL expression root
 * (the root itself plus every descendant, in the RTL model's child links).
 * Returns 0 when the root is invalid, already visited, or the model holds no
 * such record. The caller owns the out-arena, whose size must be at least the
 * model's expression count. */
static int rtest_collect_exprs(const pigen_rtl_model *model,
	pigen_rtl_expr_id root, int *seen, pigen_rtl_expr_id *out,
	size_t out_capacity, size_t *out_count)
{
	size_t child_count;
	const pigen_rtl_expr_id *children;
	const pigen_rtl_expr *record;
	size_t i;

	if (IS_INVALID_ID(root) || root.index >= model->expression_count)
		return 0;
	record = pigen_rtl_expr_get(model, root);
	if (!record)
		return 0;
	if (seen[root.index])
		return 1;
	seen[root.index] = 1;
	if (*out_count < out_capacity)
		out[(*out_count)++] = root;
	children = pigen_rtl_expr_children(model, root, &child_count);
	for (i = 0; i < child_count; i++)
		if (!rtest_collect_exprs(model, children[i], seen, out,
			out_capacity, out_count))
			return 0;
	return 1;
}

/* 1 when some non-constant RTL expression identity is reachable from BOTH
 * roots. A constant (integer) node is never a fire-identity conjunction, so
 * the two roots sharing only a lowered constant does not satisfy the
 * one-shared-fire-identity contract: this is the check that pins the fire
 * identity as a single memoized conjunction referenced by both the
 * destination update and the source-ready equation rather than re-lowered
 * into two distinct copies. */
static int rtest_shares_nonconst(const pigen_rtl_model *model,
	pigen_rtl_expr_id a, pigen_rtl_expr_id b)
{
	int *seen_a;
	int *seen_b;
	pigen_rtl_expr_id *a_arena;
	pigen_rtl_expr_id *b_arena;
	size_t a_count = 0;
	size_t b_count = 0;
	size_t capacity;
	const pigen_rtl_expr *record;
	size_t i, j;
	int found = 0;

	if (IS_INVALID_ID(a) || IS_INVALID_ID(b))
		return 0;
	capacity = model->expression_count + 1;
	seen_a = calloc(capacity, sizeof(*seen_a));
	seen_b = calloc(capacity, sizeof(*seen_b));
	a_arena = malloc(capacity * sizeof(*a_arena));
	b_arena = malloc(capacity * sizeof(*b_arena));
	if (!seen_a || !seen_b || !a_arena || !b_arena) {
		free(seen_a);
		free(seen_b);
		free(a_arena);
		free(b_arena);
		return 0;
	}
	if (!rtest_collect_exprs(model, a, seen_a, a_arena, capacity, &a_count)) {
		free(seen_a);
		free(seen_b);
		free(a_arena);
		free(b_arena);
		return 0;
	}
	if (!rtest_collect_exprs(model, b, seen_b, b_arena, capacity, &b_count)) {
		free(seen_a);
		free(seen_b);
		free(a_arena);
		free(b_arena);
		return 0;
	}
	for (i = 0; i < a_count && !found; i++) {
		for (j = 0; j < b_count; j++) {
			if (a_arena[i].index == b_arena[j].index) {
				record = pigen_rtl_expr_get(model, a_arena[i]);
				if (record && record->kind != PIGEN_RTL_EXPR_INTEGER)
					found = 1;
				break;
			}
		}
	}
	free(seen_a);
	free(seen_b);
	free(a_arena);
	free(b_arena);
	return found;
}

/* Count the DISTINCT expression identities reachable from one RTL
 * expression root (the root itself plus every descendant, in the model's
 * child links) whose record has the given kind. When operand is a valid
 * identity, only nodes that match it are counted - the node's own identity
 * equals it (a childless control leaf), or its first child equals it (the
 * negation wrapper); when it is invalid, no operand test is applied.
 * Recursion is identity-based through the model's child links: a node
 * referenced twice from the same tree is counted once, so the probe reports
 * the deduplicated conjunction shape. Returns 0 when the root is invalid or
 * the model holds no such record. */
static int rtest_count_reachable_kind(const pigen_rtl_model *model,
	pigen_rtl_expr_id root, pigen_rtl_expr_kind kind,
	pigen_rtl_expr_id operand)
{
	int *seen;
	const pigen_rtl_expr_id *children;
	const pigen_rtl_expr *record;
	size_t capacity;
	size_t child_count;
	size_t i;
	int total = 0;

	if (IS_INVALID_ID(root) || root.index >= model->expression_count)
		return 0;
	capacity = model->expression_count + 1;
	seen = calloc(capacity, sizeof(*seen));
	if (!seen)
		return 0;
	if (seen[root.index]) {
		free(seen);
		return 0;
	}
	seen[root.index] = 1;
	record = pigen_rtl_expr_get(model, root);
	if (record && record->kind == kind) {
		if (IS_INVALID_ID(operand))
			total++;
		else if (root.index == operand.index)
			total++;
		else if (record->child_count >= 1) {
			children = pigen_rtl_expr_children(model, root,
				&child_count);
			if (children && children[0].index == operand.index)
				total++;
		}
	}
	children = pigen_rtl_expr_children(model, root, &child_count);
	for (i = 0; i < child_count; i++)
		total += rtest_count_reachable_kind(model, children[i], kind,
			operand);
	free(seen);
	return total;
}

int main(int argc, char **argv)
{
	pigen_source_manager sources = {0};
	pigen_semantic_model sem;
	pigen_rtl_model rtl = {0};
	pigen_rtl_lowering lowering;

	check_init(argc, argv);
	pigen_semantic_init(&sem, &sources);
	pigen_rtl_lowering_init(&lowering, &sem, &rtl);

	SECTION("t5-skeleton") {
	/* init stores the models and leaves both identity memo maps empty. */
	REQUIRE(lowering.semantics == &sem);
	REQUIRE(lowering.rtl == &rtl);
	REQUIRE(!lowering.lowered_types &&
		!lowering.lowered_type_count && !lowering.lowered_type_capacity);
	REQUIRE(!lowering.lowered_expressions &&
		!lowering.lowered_expression_count &&
		!lowering.lowered_expression_capacity);

	/* Both entry points report the unimplemented/invalid sentinel, for a
	 * valid lowering and for a NULL lowering. */
	REQUIRE(IS_INVALID_ID(pigen_lower_rtl_type(&lowering, INVALID_TYPE)));
	REQUIRE(IS_INVALID_ID(pigen_lower_rtl_expression(&lowering, INVALID_EXPR)));
	REQUIRE(IS_INVALID_ID(pigen_lower_rtl_type(NULL, INVALID_TYPE)));
	REQUIRE(IS_INVALID_ID(pigen_lower_rtl_expression(NULL, INVALID_EXPR)));
	}

	/* (3) Type lowering preserves the owner-reported state, width, signedness
	 * and range shape. First type-family section of the Task 5 plus Task 6
	 * lowering contract, fully implemented against the landed owner APIs.
	 * The invalid-id sentinel assert below is the section's first assert and
	 * passes against the landed lowering, which reports the sentinel for an
	 * invalid id; case 1's `!IS_INVALID_ID(id)` and every later assert in this
	 * section are green against the landed type lowering. Cases 1-3 build real
	 * owner data types and assert the lowered pigen_rtl_type record matches
	 * exactly what the data-type owner reports (signedness, state domain, width
	 * and packed range shape); case 4 pins the invalid-id contract. Only the
	 * returned record is asserted - never an imagined internal representation -
	 * so the implementer cannot shortcut the preservation contract. */
	SECTION("t5-type-lowering") {
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
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem, 8, unsized);
		width12 = pigen_const_expr_intern_integer(&sem, 12, unsized);
		t_signed = pigen_data_type_signed_integer(&sem, width8);
		t_unsigned = pigen_data_type_unsigned_integer(&sem, width12);
		t_bit = pigen_data_type_sized_logic(&sem, 16, PIGEN_SIGN_UNSIGNED);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(width12));
		REQUIRE(!IS_INVALID_ID(t_signed) && !IS_INVALID_ID(t_unsigned) &&
			!IS_INVALID_ID(t_bit));

		/* Case 4 (front-loaded, the section's first behavioral assert): an
		 * invalid data-type id returns the RTL-type sentinel and leaves both
		 * identity memo maps untouched. */
		id = pigen_lower_rtl_type(&lowering, INVALID_TYPE);
		REQUIRE(IS_INVALID_ID(id));
		REQUIRE(!lowering.lowered_types &&
			!lowering.lowered_type_count && !lowering.lowered_type_capacity);
		REQUIRE(!lowering.lowered_expressions &&
			!lowering.lowered_expression_count &&
			!lowering.lowered_expression_capacity);

		/* Case 1: signed int[8] preserves signedness, state domain, width and
		 * the owner-reported range shape (dimension count, packed width and a
		 * valid packed element). */
		id = pigen_lower_rtl_type(&lowering, t_signed);
		REQUIRE(!IS_INVALID_ID(id)); /* Green against the landed lowering. */
		rt = pigen_rtl_type_get(&rtl, id);
		REQUIRE(rt);
		REQUIRE(rt->signedness == PIGEN_SIGN_SIGNED);
		REQUIRE(rt->signedness == pigen_data_type_signedness(&sem, t_signed));
		REQUIRE(rt->state_domain == pigen_data_type_state_domain(&sem, t_signed));
		REQUIRE(rt->width == 8);
		REQUIRE(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, t_signed), &w) && w == 8);
		/* Range shape: the owner's dimension count and packed element are
		 * preserved exactly on the lowered record. */
		REQUIRE(rt->dimension_count ==
			pigen_data_type_dimension_count(&sem, t_signed));
		REQUIRE(!IS_INVALID_ID(pigen_data_type_packed_element(&sem, t_signed)));

		/* Case 2: unsigned uint[12] preserves unsigned signedness, state domain,
		 * width 12 and the owner-reported range shape. */
		id = pigen_lower_rtl_type(&lowering, t_unsigned);
		REQUIRE(!IS_INVALID_ID(id));
		rt = pigen_rtl_type_get(&rtl, id);
		REQUIRE(rt);
		REQUIRE(rt->signedness == PIGEN_SIGN_UNSIGNED);
		REQUIRE(rt->signedness == pigen_data_type_signedness(&sem, t_unsigned));
		REQUIRE(rt->state_domain == pigen_data_type_state_domain(&sem, t_unsigned));
		REQUIRE(rt->width == 12);
		REQUIRE(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, t_unsigned), &w) && w == 12);
		REQUIRE(rt->dimension_count ==
			pigen_data_type_dimension_count(&sem, t_unsigned));
		REQUIRE(!IS_INVALID_ID(pigen_data_type_packed_element(&sem, t_unsigned)));

		/* Case 3: bit[16] preserves width 16, the state domain exactly as the
		 * owner reports it, and the owner's single packed range. */
		id = pigen_lower_rtl_type(&lowering, t_bit);
		REQUIRE(!IS_INVALID_ID(id));
		rt = pigen_rtl_type_get(&rtl, id);
		REQUIRE(rt);
		REQUIRE(rt->signedness == PIGEN_SIGN_UNSIGNED);
		REQUIRE(rt->signedness == pigen_data_type_signedness(&sem, t_bit));
		REQUIRE(rt->state_domain == pigen_data_type_state_domain(&sem, t_bit));
		REQUIRE(rt->width == 16);
		REQUIRE(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, t_bit), &w) && w == 16);
		/* bit[16] carries the owner's single packed range: the lowered record
		 * keeps that one dimension (the owner reports [15:0]). */
		REQUIRE(pigen_data_type_dimension_count(&sem, t_bit) == 1);
		REQUIRE(rt->dimension_count == 1 && rt->dimensions);
	}

	/* (4) Expression lowering preserves the surviving width, signedness, every
	 * explicit conversion/projection and child order. Second expression-family
	 * section of the Task 5 plus Task 6 lowering contract, fully implemented
	 * against the landed owner APIs. The front-loaded invalid-expression-id
	 * sentinel assert below is the section's first assert and passes against the
	 * landed lowering; case 1's `!IS_INVALID_ID(eid)` and every later assert in
	 * this section are green against the landed expression lowering. Cases 1-4
	 * build real owner constant expressions through the owner APIs and assert
	 * only the returned pigen_rtl_expr record: its kind, the result type's
	 * signedness and width exactly as the owner's resolved result data type
	 * reports them, every explicit conversion and select kept on the record, and
	 * the lowered child count and order. Child order is asserted by identity:
	 * the owner's source children (as.binary.left/right, as.conversion.operand,
	 * as.select.base/left/right, as.sequence via pigen_const_expr_children)
	 * lowered by identity must be exactly the lowered children, in order - which
	 * a reordered or dropped-children implementation cannot shortcut. Case 5
	 * pins the invalid-expression-id contract. */
	SECTION("t5-expression-lowering") {
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
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem, 8, unsized);
		t8 = pigen_data_type_signed_integer(&sem, width8);
		t12 = pigen_data_type_unsigned_integer(&sem,
			pigen_const_expr_intern_integer(&sem, 12, unsized));
		t16 = pigen_data_type_sized_logic(&sem, 16, PIGEN_SIGN_UNSIGNED);
		t4 = pigen_data_type_sized_logic(&sem, 4, PIGEN_SIGN_UNSIGNED);
		REQUIRE(!IS_INVALID_ID(t8) && !IS_INVALID_ID(t12) &&
			!IS_INVALID_ID(t16) && !IS_INVALID_ID(t4));
		a = pigen_const_expr_intern_integer(&sem, 1, t8);
		b = pigen_const_expr_intern_integer(&sem, 2, t8);
		base16 = pigen_const_expr_intern_integer(&sem, 0x1234, t16);
		idx5 = pigen_const_expr_intern_integer(&sem, 5, unsized);
		c4 = pigen_const_expr_intern_integer(&sem, 0xAB, t4);
		c16 = pigen_const_expr_intern_integer(&sem, 0x5678, t16);
		REQUIRE(!IS_INVALID_ID(a) && !IS_INVALID_ID(b) &&
			!IS_INVALID_ID(base16) && !IS_INVALID_ID(idx5) &&
			!IS_INVALID_ID(c4) && !IS_INVALID_ID(c16));

		/* Case 5 (front-loaded, the section's first behavioral assert): an
		 * invalid constant-expression id returns the RTL-expression sentinel
		 * and leaves both identity memo maps untouched. */
		eid = pigen_lower_rtl_expression(&lowering, INVALID_EXPR);
		REQUIRE(IS_INVALID_ID(eid));
		REQUIRE(!lowering.lowered_expressions &&
			!lowering.lowered_expression_count &&
			!lowering.lowered_expression_capacity);

		/* Case 1: widening binary arithmetic. signed int[8] + signed int[8]
		 * resolves through the owner to a signed result type strictly wider
		 * than the operands; the lowered record keeps the binary kind, the
		 * owner's result type's signedness and width, and both children in
		 * source child order (left then right). */
		REQUIRE(pigen_data_type_resolve_binary_operation(&sem, PIGEN_BINARY_ADD,
			t8, t8, &binary_resolution));
		add_expr = pigen_const_expr_intern_binary(&sem,
			binary_resolution.operation, a, b);
		owner_expr = pigen_const_expr_get(&sem, add_expr);
		REQUIRE(owner_expr && owner_expr->kind == PIGEN_CONST_EXPR_BINARY);
		REQUIRE(!IS_INVALID_ID(owner_expr->data_type));
		REQUIRE(pigen_data_type_signedness(&sem, owner_expr->data_type) ==
			PIGEN_SIGN_SIGNED);
		REQUIRE(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, owner_expr->data_type), &w));
		REQUIRE(w > 8); /* widening: the result is wider than the int[8] operands */
		eid = pigen_lower_rtl_expression(&lowering, add_expr);
		REQUIRE(!IS_INVALID_ID(eid)); /* Green against the landed lowering. */
		re = pigen_rtl_expr_get(&rtl, eid);
		REQUIRE(re && re->kind == PIGEN_RTL_EXPR_BINARY);
		result_type = pigen_rtl_type_get(&rtl, re->type);
		REQUIRE(result_type);
		REQUIRE(result_type->signedness ==
			pigen_data_type_signedness(&sem, owner_expr->data_type));
		REQUIRE(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, owner_expr->data_type), &w));
		REQUIRE(result_type->width == w);
		/* Both children lowered, in source child order, by identity. */
		rtl_children = pigen_rtl_expr_children(&rtl, eid, &rtl_child_count);
		REQUIRE(rtl_children && rtl_child_count == 2);
		REQUIRE(rtl_children[0].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.binary.left).index);
		REQUIRE(rtl_children[1].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.binary.right).index);

		/* Case 2: explicit cast. An owner-resolved explicit conversion from
		 * bit[16] to signed int[12] is interned as a conversion constant;
		 * the lowered record keeps the conversion verbatim and the target
		 * type's signedness and width. */
		REQUIRE(pigen_data_type_resolve_explicit_conversion(&sem, t16, t8,
			&conversion));
		REQUIRE(pigen_data_type_conversion_is_valid(&sem, conversion));
		conv_expr = pigen_const_expr_intern_conversion(&sem, conversion, base16);
		owner_expr = pigen_const_expr_get(&sem, conv_expr);
		REQUIRE(owner_expr && owner_expr->kind == PIGEN_CONST_EXPR_CONVERSION);
		REQUIRE(!IS_INVALID_ID(owner_expr->data_type));
		REQUIRE(owner_expr->as.conversion.conversion.kind == conversion.kind);
		REQUIRE(owner_expr->as.conversion.conversion.source_data_type.index ==
			conversion.source_data_type.index);
		REQUIRE(owner_expr->as.conversion.conversion.target_data_type.index ==
			conversion.target_data_type.index);
		REQUIRE(pigen_data_type_signedness(&sem, owner_expr->data_type) ==
			PIGEN_SIGN_SIGNED);
		eid = pigen_lower_rtl_expression(&lowering, conv_expr);
		REQUIRE(!IS_INVALID_ID(eid));
		re = pigen_rtl_expr_get(&rtl, eid);
		REQUIRE(re && re->kind == PIGEN_RTL_EXPR_CONVERSION);
		REQUIRE(re->as.conversion.conversion.kind == conversion.kind);
		REQUIRE(re->as.conversion.conversion.source_data_type.index ==
			conversion.source_data_type.index);
		REQUIRE(re->as.conversion.conversion.target_data_type.index ==
			conversion.target_data_type.index);
		result_type = pigen_rtl_type_get(&rtl, re->type);
		REQUIRE(result_type);
		REQUIRE(result_type->signedness ==
			pigen_data_type_signedness(&sem, owner_expr->data_type));
		REQUIRE(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, owner_expr->data_type), &w));
		REQUIRE(result_type->width == w);
		/* The single operand is lowered and kept, by identity. */
		rtl_children = pigen_rtl_expr_children(&rtl, eid, &rtl_child_count);
		REQUIRE(rtl_children && rtl_child_count == 1);
		REQUIRE(rtl_children[0].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.conversion.operand).index);

		/* Case 3: projection (select). A constant bit[15:0] range-selected to
		 * bits [5:0] through the owner keeps the select kind and the
		 * owner's projected width on the lowered record. */
		select_type = pigen_data_type_packed_select(&sem, t16, idx5,
			pigen_const_expr_intern_integer(&sem, 0, unsized),
			PIGEN_SEMANTIC_SELECT_RANGE);
		REQUIRE(!IS_INVALID_ID(select_type));
		REQUIRE(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, select_type), &w) && w == 6);
		select_expr = pigen_const_expr_intern_select(&sem, base16, idx5,
			pigen_const_expr_intern_integer(&sem, 0, unsized),
			PIGEN_SEMANTIC_SELECT_RANGE, select_type);
		owner_expr = pigen_const_expr_get(&sem, select_expr);
		REQUIRE(owner_expr && owner_expr->kind == PIGEN_CONST_EXPR_SELECT);
		REQUIRE(!IS_INVALID_ID(owner_expr->data_type));
		REQUIRE(owner_expr->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE);
		eid = pigen_lower_rtl_expression(&lowering, select_expr);
		REQUIRE(!IS_INVALID_ID(eid));
		re = pigen_rtl_expr_get(&rtl, eid);
		REQUIRE(re && re->kind == PIGEN_RTL_EXPR_SELECT);
		REQUIRE(re->as.select.kind == owner_expr->as.select.kind);
		result_type = pigen_rtl_type_get(&rtl, re->type);
		REQUIRE(result_type);
		REQUIRE(pigen_const_expr_evaluate_u64(&sem,
			pigen_data_type_packed_width(&sem, owner_expr->data_type), &w));
		REQUIRE(result_type->width == w);
		/* Base and range bounds lowered, in owner child order (base, left,
		 * right), by identity. */
		rtl_children = pigen_rtl_expr_children(&rtl, eid, &rtl_child_count);
		REQUIRE(rtl_children && rtl_child_count == 3);
		REQUIRE(rtl_children[0].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.select.base).index);
		REQUIRE(rtl_children[1].index ==
			pigen_lower_rtl_expression(&lowering,
				owner_expr->as.select.left).index);
		REQUIRE(rtl_children[2].index ==
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
			REQUIRE(!IS_INVALID_ID(concat_type));
			REQUIRE(pigen_const_expr_evaluate_u64(&sem,
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
			REQUIRE(owner_expr &&
				owner_expr->kind == PIGEN_CONST_EXPR_CONCATENATION);
			REQUIRE(!IS_INVALID_ID(owner_expr->data_type));
			eid = pigen_lower_rtl_expression(&lowering, concat_expr);
			REQUIRE(!IS_INVALID_ID(eid));
			re = pigen_rtl_expr_get(&rtl, eid);
			REQUIRE(re && re->kind == PIGEN_RTL_EXPR_CONCATENATION);
			result_type = pigen_rtl_type_get(&rtl, re->type);
			REQUIRE(result_type);
			REQUIRE(pigen_const_expr_evaluate_u64(&sem,
				pigen_data_type_packed_width(&sem, owner_expr->data_type),
				&w));
			REQUIRE(result_type->width == w);
			/* Owner's children via the children arena, in source order. */
			seq_owner_children = pigen_const_expr_children(&sem,
				owner_expr->as.sequence.first_child,
				owner_expr->as.sequence.child_count);
			REQUIRE(seq_owner_children &&
				owner_expr->as.sequence.child_count == 2);
			REQUIRE(seq_owner_children[0].index == c4.index);
			REQUIRE(seq_owner_children[1].index == c16.index);
			rtl_children = pigen_rtl_expr_children(&rtl, eid, &rtl_child_count);
			REQUIRE(rtl_children && rtl_child_count == 2);
			for (i = 0; i < rtl_child_count; i++)
				REQUIRE(rtl_children[i].index ==
					pigen_lower_rtl_expression(&lowering,
						seq_owner_children[i]).index);
		}
	}

	/* (5) Constants are lowered once by identity: the same source constant
	 * expression lowered twice returns the same memoized RTL handle and does
	 * not grow the RTL expression arena, a distinct constant of the same type
	 * gets a different handle, and the identity memo slot is stable. Third
	 * expression-family section of the Task 5 plus Task 6 lowering contract
	 * (the constant-identity family), fully implemented against the landed
	 * owner APIs. The front-loaded invalid-constant sentinel assert below is
	 * the section's first assert and passes against the landed lowering; case
	 * 1's `!IS_INVALID_ID(eid)` and every later assert in this section are green
	 * against the landed constant lowering. Cases 1-3 build real owner constant
	 * expressions through the owner APIs and assert only the returned
	 * pigen_rtl_expr_id and the public RTL expression-arena count - never an
	 * imagined internal memo representation - so the implementer cannot
	 * shortcut the once-by-identity contract. Case 4 pins the
	 * invalid-constant-expression contract. */
	SECTION("t5-constant-identity") {
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
		REQUIRE(!IS_INVALID_ID(unsized));
		width16 = pigen_const_expr_intern_integer(&sem, 16, unsized);
		t16 = pigen_data_type_unsigned_integer(&sem, width16);
		REQUIRE(!IS_INVALID_ID(width16) && !IS_INVALID_ID(t16));
		c5 = pigen_const_expr_intern_integer(&sem, 5, t16);
		c7 = pigen_const_expr_intern_integer(&sem, 7, t16);
		REQUIRE(!IS_INVALID_ID(c5) && !IS_INVALID_ID(c7));
		REQUIRE(c5.index != c7.index); /* distinct arena indices, same type */
		/* Same value re-interned is deduplicated by the owner to the same arena
		 * index, so it is not a distinct constant. */
		REQUIRE(c5.index ==
			pigen_const_expr_intern_integer(&sem, 5, t16).index);

		/* Case 4 (front-loaded, the section's first behavioral assert): an
		 * invalid constant-expression id returns the RTL-expression sentinel. */
		eid = pigen_lower_rtl_expression(&lowering, INVALID_EXPR);
		REQUIRE(IS_INVALID_ID(eid));

		/* Case 1: lower the SAME constant (c5) twice. Both calls return the
		 * same valid memoized RTL handle and the second call does not grow the
		 * RTL expression arena. */
		eid = pigen_lower_rtl_expression(&lowering, c5);
		REQUIRE(!IS_INVALID_ID(eid)); /* Green against the landed lowering. */
		arena_before = rtl.expression_count;
		eid_again = pigen_lower_rtl_expression(&lowering, c5);
		arena_after = rtl.expression_count;
		REQUIRE(eid_again.index == eid.index); /* memoized: same handle */
		REQUIRE(arena_after == arena_before); /* no arena growth on the second call */

		/* Case 2: a distinct constant of the same type (c7) gets a DIFFERENT
		 * valid RTL handle - identity is by arena index, not by value or type. */
		eid_distinct = pigen_lower_rtl_expression(&lowering, c7);
		REQUIRE(!IS_INVALID_ID(eid_distinct));
		REQUIRE(eid_distinct.index != eid.index);

		/* Case 3: the identity memo slot is stable: the slot keyed by the
		 * constant's arena index equals the returned handle. */
		REQUIRE(lowering.lowered_expression_count > c5.index);
		REQUIRE(lowering.lowered_expressions[c5.index].index == eid.index);

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
		REQUIRE(re);
		REQUIRE(re->kind == PIGEN_RTL_EXPR_INTEGER);
		REQUIRE(pigen_const_expr_evaluate_u64(&sem, c5, &w));
		REQUIRE(re->value == w); /* owner-reported value (5) on the record */
		REQUIRE(re->type.index ==
			pigen_lower_rtl_type(&lowering, t16).index);

		/* c7 (the lowered value 7, the distinct constant) carries the same
		 * integer kind, its own owner-reported value and the same lowered
		 * t16 type. */
		re = pigen_rtl_expr_get(&rtl, eid_distinct);
		REQUIRE(re);
		REQUIRE(re->kind == PIGEN_RTL_EXPR_INTEGER);
		REQUIRE(pigen_const_expr_evaluate_u64(&sem, c7, &w));
		REQUIRE(re->value == w); /* owner-reported value (7) on the record */
		REQUIRE(re->type.index ==
			pigen_lower_rtl_type(&lowering, t16).index);

		/* Case 5: an exact-integer constant lowers IDENTICALLY to the bare
		 * PIGEN_CONST_EXPR_INTEGER constant. This closes the TEST-AUDIT gap:
		 * every owner-constructable constant kind was pinned except
		 * PIGEN_CONST_EXPR_EXACT_INTEGER, so its lowering was unenforced. The
		 * confirmed contract (manager decision): an owner-constructable
		 * exact-integer constant IS lowerable, and the lowered record must
		 * publish re->kind == PIGEN_RTL_EXPR_INTEGER, re->value == the
		 * owner-reported value, and re->type.index == the lowered id of the
		 * exact-integer data type. Only the returned pigen_rtl_expr record and
		 * owner-reported facts are asserted - never an imagined internal
		 * representation - so the implementer cannot shortcut the
		 * identical-to-bare-integer contract. Case 5 now also pins the
		 * exact-integer TYPE's own lowered width: it is derived from the owner
		 * exact value (width 3 for 5), NOT the packed width - and the
		 * packed-width query is INVALID_ID for this constructor, so the
		 * value-derived width is provably not the packed width. */
		{
			pigen_integer_id v;
			pigen_data_type_id t_ex;
			pigen_const_expr_id cx;
			pigen_rtl_expr_id exid;
			const pigen_rtl_expr *rex;
			const pigen_rtl_type *rt;

			/* Owner witness: intern the exact value 5, build the exact-integer
			 * data type carrying it, and intern the exact-integer constant of
			 * that type. Identity is by (kind, value): even though the value
			 * equals c5's 5, the distinct kind keeps it at a distinct arena
			 * index. */
			v = pigen_integer_intern_u64(&sem, 5);
			REQUIRE(!IS_INVALID_ID(v));
			t_ex = pigen_data_type_exact_integer(&sem, v);
			REQUIRE(!IS_INVALID_ID(t_ex));
			cx = pigen_const_expr_intern_exact_integer(&sem, v, t_ex);
			REQUIRE(!IS_INVALID_ID(cx));
			REQUIRE(cx.index != c5.index); /* distinct from INTEGER c5, same 5 */

			/* The exact-integer constant lowers to a valid RTL handle distinct
			 * from the bare integer constant's handle: the landed lowering
			 * publishes EXACT_INTEGER with a width derived from the owner
			 * exact value, so every assert in this case is green against the
			 * landed exact-integer lowering. */
			exid = pigen_lower_rtl_expression(&lowering, cx);
			REQUIRE(!IS_INVALID_ID(exid)); /* Green against the landed lowering. */
			REQUIRE(exid.index != eid.index);

			/* The lowered record is an INTEGER carrying the owner-reported
			 * value and the lowered id of its owner type t_ex. */
			rex = pigen_rtl_expr_get(&rtl, exid);
			REQUIRE(rex);
			REQUIRE(rex->kind == PIGEN_RTL_EXPR_INTEGER);
			REQUIRE(pigen_const_expr_evaluate_u64(&sem, cx, &w));
			REQUIRE(rex->value == w); /* owner-reported value (5) on the record */
			REQUIRE(rex->type.index ==
				pigen_lower_rtl_type(&lowering, t_ex).index);

			/* TEST-AUDIT-9: pin the exact-integer TYPE's own lowered width. It
			 * must be DERIVED FROM THE OWNER EXACT VALUE (width 3 for 5), not the
			 * packed width. rt is the lowered id of t_ex; its width equals the
			 * owner unsigned width of the exact value. */
			rt = pigen_rtl_type_get(&rtl, pigen_lower_rtl_type(&lowering, t_ex));
			REQUIRE(rt);
			REQUIRE(rt->width == pigen_integer_unsigned_width(&sem, v));
			/* The packed-width query reports INVALID_ID for this constructor,
			 * so the value-derived width above is provably NOT the packed width. */
			REQUIRE(IS_INVALID_ID(pigen_data_type_packed_width(&sem, t_ex)));

			/* Boundary: a NEGATIVE exact integer is not a u64 constant. Its
			 * owner value does not evaluate to u64, and its lowering is pinned
			 * consistently to the sentinel - the one behavior the impl contract
			 * permits for a non-lowerable (negative) exact integer. */
			{
				pigen_integer_id v2;
				pigen_data_type_id t_ex2;
				pigen_const_expr_id cx2;
				pigen_rtl_expr_id exid2;

				v2 = pigen_integer_negate(&sem, v);
				REQUIRE(!IS_INVALID_ID(v2));
				t_ex2 = pigen_data_type_exact_integer(&sem, v2);
				REQUIRE(!IS_INVALID_ID(t_ex2));
				cx2 = pigen_const_expr_intern_exact_integer(&sem, v2, t_ex2);
				REQUIRE(!IS_INVALID_ID(cx2));
				REQUIRE(!pigen_const_expr_evaluate_u64(&sem, cx2, &w));
				exid2 = pigen_lower_rtl_expression(&lowering, cx2);
				REQUIRE(IS_INVALID_ID(exid2)); /* negative: not a u64 constant */
			}
		}
	}

	/* (6) Error reporting: an unbound signal and an absent expression each
	 * report the failure as the RTL-expression sentinel. Fourth
	 * expression-family section of the Task 5 plus Task 6 lowering contract
	 * (the error-reporting family), fully implemented against the landed
	 * owner APIs. A failed call may leave partial state behind: the contract
	 * pins the reported sentinel, not the state a failure leaves. Cases build
	 * one real validated model through the owner APIs (a source file, a
	 * compilation scope, a module, a declared signal, and a distinct UNBOUND
	 * PARAMETER symbol), then drive the two failure modes and assert only the
	 * reported sentinels:
	 *   Case 1: a constant expression referencing the unbound PARAMETER symbol
	 *     lowers to the RTL-expression sentinel. The witness is a distinct
	 *     PARAMETER symbol (not the real signal): the const-expr owner
	 *     interns only PIGEN_SYMBOL_PARAMETER symbols, and
	 *     pigen_signal_add requires PIGEN_SYMBOL_SIGNAL, so the same symbol
	 *     cannot serve both.
	 *   Case 2: a constant-expression id that does not exist in the semantic
	 *     arena lowers to the sentinel. The landed lowering reports the
	 *     sentinel for this absent id.
	 *   Case 3: after both failures an already-lowered valid constant still
	 *     returns its memoized valid RTL handle - memo stability across the
	 *     failures. The landed lowering memoizes the valid constant, so the
	 *     handle survives both failures.
	 * Every assert in this section is green against the landed lowering,
	 * which reports the sentinel for these failure inputs and memoizes the
	 * valid constant; every former red marker is retired in this file. */
	SECTION("t5-error-reporting") {
		const char *text =
			"module top : input value : logic ;\n";
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id signal_symbol;
		pigen_symbol_id param_symbol;
		pigen_module_id module;
		pigen_signal_id signal;
		pigen_data_type_id t16;
		pigen_const_expr_id width16;
		pigen_const_expr_id good;
		pigen_const_expr_id sym_expr;
		pigen_rtl_expr_id good_id;
		pigen_rtl_expr_id eid;
		pigen_rtl_expr_id recoverable;

		/* Build the signal's 16-bit type first, then the real validated owner
		 * model: a source file, the compilation scope, a module in it, and one
		 * declared signal of that type. The spans below are chosen so every
		 * owner span constraint holds: the module symbol's declaration spans
		 * the whole file (as module_add requires), the module scope uses the
		 * same span, and the signal symbol's name span is contained in and its
		 * declaration span equal to the span signal_add checks. */
		width16 = pigen_const_expr_intern_integer(&sem, 16,
			pigen_data_type_unsized_integer(&sem));
		REQUIRE(!IS_INVALID_ID(width16));
		t16 = pigen_data_type_unsigned_integer(&sem, width16);
		REQUIRE(!IS_INVALID_ID(t16));
		source = pigen_source_add(&sources, "lower_error.pigen", text,
			strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name = (pigen_source_span){source, 19, 24}; /* "value" */
		sem.compilation_scope = pigen_scope_add(&sem,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem, sem.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem, sem.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem, module_scope, PIGEN_SYMBOL_SIGNAL,
			t16, name, name, &signal_symbol, NULL) == PIGEN_DECLARE_OK);
		signal = pigen_signal_add(&sem, (pigen_syntax_id){2}, module,
			signal_symbol, t16, pigen_semantic_scalar_shape(&sem),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
			PIGEN_SEMANTIC_INTERNAL, name);
		REQUIRE(signal.index != PIGEN_INVALID_ID);

		/* A DISTINCT unbound PARAMETER symbol: declared in the module scope but
		 * never given a parameter value (no pigen_parameter_add), so it carries
		 * no value - an unbound witness. The const-expr owner interns only
		 * PIGEN_SYMBOL_PARAMETER symbols, and pigen_signal_add requires
		 * PIGEN_SYMBOL_SIGNAL, so the real signal symbol above cannot serve as
		 * the const-expr witness: the two symbols are distinct and both are
		 * kept - the real signal build stays intact (it exercises the
		 * memo-stability path). */
		/* The parameter name span is "top" in the same source file: it is
		 * valid, non-empty, and does not collide with the "value" signal
		 * symbol in module_scope (the module symbol owns "top" in the parent
		 * scope, which lookup_local does not see). */
		REQUIRE(pigen_symbol_declare(&sem, module_scope, PIGEN_SYMBOL_PARAMETER,
			t16, (pigen_source_span){source, 7, 10},
			(pigen_source_span){source, 7, 10}, &param_symbol, NULL) ==
			PIGEN_DECLARE_OK);

		/* A valid constant of the same type, lowered before any failure, is
		 * the memo-stability witness for Case 3. */
		good = pigen_const_expr_intern_integer(&sem, 5, t16);
		REQUIRE(!IS_INVALID_ID(good));
		good_id = pigen_lower_rtl_expression(&lowering, good);
		REQUIRE(!IS_INVALID_ID(good_id)); /* Green against the landed lowering
		 * (the memoized valid constant). */

		/* Case 1: a constant expression referencing the unbound PARAMETER
		 * symbol reports the sentinel: the landed lowering reports the sentinel
		 * for these failure inputs. The witness is a PARAMETER symbol because
		 * the const-expr owner interns only PIGEN_SYMBOL_PARAMETER symbols, and
		 * it is unbound (no parameter value) so lowering must report it. */
		sym_expr = pigen_const_expr_intern_symbol(&sem, param_symbol, t16);
		REQUIRE(!IS_INVALID_ID(sym_expr));
		eid = pigen_lower_rtl_expression(&lowering, sym_expr);
		REQUIRE(IS_INVALID_ID(eid));

		/* Case 2: a constant-expression id absent from the semantic arena
		 * reports the sentinel. */
		eid = pigen_lower_rtl_expression(&lowering,
			(pigen_const_expr_id){999});
		REQUIRE(IS_INVALID_ID(eid));

		/* Case 3: after both failures the already-lowered valid constant
		 * still returns its memoized valid handle - memo stability across
		 * the failures. */
		recoverable = pigen_lower_rtl_expression(&lowering, good);
		REQUIRE(!IS_INVALID_ID(recoverable));
		REQUIRE(recoverable.index == good_id.index);
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
	 * Green: every assert in this section passes against the landed Task 5
	 * type/expression lowering, including Case 1's first
	 * `!IS_INVALID_ID(id1)` (second pass, t8s), which resolves to a valid
	 * pigen_rtl_type_id - the memo map publishes one record per identity,
	 * so the second pass's valid-id check holds first and every later assert
	 * in this section holds as well. No stale red markers remain in this file.
	 * This is the LAST test-contract section of the chain: the target is
	 * green end to end. */
	SECTION("t5-memoization") {
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
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem, 8, unsized);
		width16 = pigen_const_expr_intern_integer(&sem, 16, unsized);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(width16));
		t8s = pigen_data_type_signed_integer(&sem, width8);
		t16u = pigen_data_type_unsigned_integer(&sem, width16);
		REQUIRE(!IS_INVALID_ID(t8s) && !IS_INVALID_ID(t16u));
		c5 = pigen_const_expr_intern_integer(&sem, 5, t8s);
		c0x1234 = pigen_const_expr_intern_integer(&sem, 0x1234, t16u);
		REQUIRE(!IS_INVALID_ID(c5) && !IS_INVALID_ID(c0x1234));
		REQUIRE(pigen_data_type_resolve_explicit_conversion(&sem, t16u, t8s,
			&conversion));
		REQUIRE(pigen_data_type_conversion_is_valid(&sem, conversion));
		conv = pigen_const_expr_intern_conversion(&sem, conversion, c0x1234);
		REQUIRE(!IS_INVALID_ID(conv));

		/* Case 1: REPEAT-LOWER STABILITY. Lower the whole fixed set (types,
		 * then constants) and record every returned id, then lower the whole
		 * set AGAIN and assert every returned id is identical, the memo map
		 * counts and the RTL arena counts are unchanged - no duplicate growth.
		 * The second pass's valid-id checks pass against the landed
		 * lowering, which publishes one record per identity. */
		/* First pass: lower the whole fixed set and record every returned id.
		 * The landed lowering resolves each identity, so every recorded id
		 * is valid - the second pass below re-lowers the same set. */
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
		REQUIRE(!IS_INVALID_ID(id1)); /* Green: the landed lowering
		 * resolves the valid type to a pigen_rtl_type_id. */
		REQUIRE(id1.index == p1_t8s.index);
		REQUIRE(pigen_lower_rtl_type(&lowering, t16u).index == p1_t16u.index);
		REQUIRE(pigen_lower_rtl_expression(&lowering, c5).index == p1_c5.index);
		REQUIRE(pigen_lower_rtl_expression(&lowering, c0x1234).index ==
			p1_c16.index);
		REQUIRE(pigen_lower_rtl_expression(&lowering, conv).index ==
			p1_conv.index);
		type_count_after = rtl.type_count;
		expr_count_after = rtl.expression_count;
		type_map_after = lowering.lowered_type_count;
		expr_map_after = lowering.lowered_expression_count;
		stable = type_count_after == type_count_before &&
			expr_count_after == expr_count_before &&
			type_map_after == type_map_before &&
			expr_map_after == expr_map_before;
		REQUIRE(stable); /* memoized: no duplicate growth on the repeat pass */

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
			REQUIRE(!IS_INVALID_ID(unsized2));
			width8_2 = pigen_const_expr_intern_integer(&sem2, 8, unsized2);
			width16_2 = pigen_const_expr_intern_integer(&sem2, 16, unsized2);
			REQUIRE(!IS_INVALID_ID(width8_2) && !IS_INVALID_ID(width16_2));
			t8s2 = pigen_data_type_signed_integer(&sem2, width8_2);
			t16u2 = pigen_data_type_unsigned_integer(&sem2, width16_2);
			REQUIRE(!IS_INVALID_ID(t8s2) && !IS_INVALID_ID(t16u2));
			c5_2 = pigen_const_expr_intern_integer(&sem2, 5, t8s2);
			c0x1234_2 = pigen_const_expr_intern_integer(&sem2, 0x1234, t16u2);
			REQUIRE(!IS_INVALID_ID(c5_2) && !IS_INVALID_ID(c0x1234_2));
			REQUIRE(pigen_data_type_resolve_explicit_conversion(&sem2, t16u2,
				t8s2, &conversion2));
			REQUIRE(pigen_data_type_conversion_is_valid(&sem2, conversion2));
			conv_2 = pigen_const_expr_intern_conversion(&sem2, conversion2,
				c0x1234_2);
			REQUIRE(!IS_INVALID_ID(conv_2));

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
			REQUIRE(deterministic); /* Green: both builds publish identical records. */

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
			REQUIRE(deterministic); /* Green: both builds publish identical records. */

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
			REQUIRE(deterministic); /* Green: both builds publish identical records. */

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
						(kids1[0].index - base1) ==
						(kids2[0].index - base2);
				}
			}
			REQUIRE(deterministic); /* Green: both builds publish identical records. */

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
			REQUIRE(rt); /* Green: the type memo map slots resolve in the model. */
		}
		for (i = 0; i < lowering.lowered_expression_count; i++) {
			if (lowering.lowered_expressions[i].index == PIGEN_INVALID_ID)
				continue;
			re = pigen_rtl_expr_get(&rtl, lowering.lowered_expressions[i]);
			REQUIRE(re); /* Green: the expression memo map slots resolve in the model. */
		}
	}

	/* (8) Boundary realization (module input): the BOUNDARY endpoint
	 * contract for pigen_lower_rtl_module_declarations. First
	 * declaration-family section of the Task 6 test-contract chain. BOUNDARY
	 * is module-input only: ABSTRACT is the only source transfer type that
	 * maps to the BOUNDARY realization, its descriptor has is_concrete=0, and
	 * pigen_signal_add forces the INPUT direction for non-concrete types and
	 * rejects NULL transfer-type descriptors - so no OUTPUT-direction
	 * boundary signal and no NULL-descriptor/INVALID-realization signal are
	 * constructible through the owner API, and neither is attempted here.
	 * The model is built through the owner APIs exactly as the landed
	 * construction pattern does: a source file, the compilation scope, a
	 * module symbol + pigen_module_add, a PIGEN_SYMBOL_SIGNAL declaration
	 * (matching data type and declaration span) and pigen_signal_add with
	 * PIGEN_TRANSFER_TYPE_ABSTRACT and PIGEN_SEMANTIC_INPUT. Checked through
	 * pigen_rtl_lowering_init + pigen_lower_rtl_module_declarations:
	 *   Case 1: THREE-PORT SHAPE. The call reports success and the
	 *     pigen_rtl_signal_endpoints record for that signal holds distinct
	 *     payload, valid and ready - payload a real pigen_rtl_object_id with
	 *     a resolvable RTL object record, valid and ready real
	 *     pigen_rtl_expr_id handles with resolvable RTL expression records -
	 *     and the input side exposes the SAME three objects as the
	 *     declaration's input side (input_payload/input_valid/input_ready).
	 *   Case 2: CONTEXT-DEPENDENT CONTROLS. The ABSTRACT descriptor carries
	 *     valid_constant=-1 and ready_constant=-1 and the BOUNDARY
	 *     realization has ready_dependency PIGEN_TRANSFER_READY_EXTERNAL, so
	 *     a boundary signal has NO constant controls: the valid and ready
	 *     expression records must NOT be the owner-published constant
	 *     expression for a 1-bit constant (lowered through
	 *     pigen_const_expr_intern_integer + pigen_lower_rtl_expression on a
	 *     SEPARATE lowering/model, whose record is the constant witness).
	 *     The constant-expression-identity contract belongs to section (9);
	 *     no constant value is pinned here.
	 *   Case 3: MEMO/STABILITY. A second call of
	 *     pigen_lower_rtl_module_declarations on the SAME lowering is
	 *     idempotent (no duplicate objects/expressions, endpoints record
	 *     unchanged), and a second independently built model yields
	 *     identical endpoint ids at equal offsets.
	 *   Case 4: FAILURE REPORT. A genuinely different failing module in
	 *     the SAME lowering - a second module/scope with one ABSTRACT
	 *     INPUT signal whose data type is a NEGATIVE exact integer, the
	 *     stable owner-constructible failure witness (the type exists in
	 *     the semantic owner but pigen_lower_rtl_type refuses it, so the
	 *     declaration lowering must report -1 regardless of the adapter
	 *     table's state). The contract pins the reported -1, not the state
	 *     a failed call leaves. The transient unimplemented-adapter path is
	 *     deliberately not the frozen failure witness: it cannot by itself
	 *     justify a frozen failure assertion once every promised adapter is
	 *     implemented.
	 * Green: every owner-construction and descriptor assert passes against
	 * the landed owners, and every lowering-behavior assert passes against
	 * the landed Task 6 BOUNDARY declaration lowering: Case 1's `rc == 0`
	 * reports success from the implementation, not a compile or
	 * harness error. Every later assert in this section holds as well.
	 * No stale red markers remain in this file. */
	SECTION("t6-boundary") {
		const char *text =
			"module top : input value : abstract ;\n";
		pigen_source_manager sources_b = {0};
		pigen_semantic_model sem_b;
		pigen_rtl_model rtl_b = {0};
		pigen_rtl_lowering lowering_b;
		pigen_data_type_id unsized;
		pigen_data_type_id t8;
		pigen_const_expr_id width8;
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id signal_symbol;
		pigen_module_id module;
		pigen_signal_id signal;
		const pigen_semantic_signal *owner_signal;
		const pigen_transfer_type_descriptor *descriptor;
		const pigen_transfer_realization_descriptor *realization;
		const pigen_rtl_signal_endpoints *endpoints;
		const pigen_rtl_object *payload_obj;
		const pigen_rtl_expr *valid_re;
		const pigen_rtl_expr *ready_re;
		const pigen_rtl_signal_endpoints *endpoints_again;
		const pigen_rtl_object *input_obj;
		pigen_rtl_object_id input_payload_obj;
		pigen_rtl_object_id input_ready_obj;
		pigen_rtl_expr_id input_valid_expr;
		size_t object_count_before;
		size_t expression_count_before;
		size_t endpoint_map_before;
		pigen_rtl_signal_endpoints record_before;
		pigen_rtl_signal_endpoints record_again;
		pigen_rtl_type_id lowered_t8;
		const pigen_rtl_type *t8_record;
		int rc;
		int stable;
		int distinct;
		int payload_shape;
		int not_constant;

		/* Build the boundary module through the owner APIs (the landed
		 * construction pattern): the signal's 8-bit type first, then a
		 * source file, the compilation scope, a module in it, and one
		 * declared ABSTRACT input signal of that type. The spans hold the
		 * owner constraints: the module symbol's declaration spans the
		 * whole file (as module_add requires), the module scope uses the
		 * same span, and the signal symbol's name span is contained in and
		 * its declaration span equal to the span signal_add checks. */
		pigen_semantic_init(&sem_b, &sources_b);
		pigen_rtl_lowering_init(&lowering_b, &sem_b, &rtl_b);
		unsized = pigen_data_type_unsized_integer(&sem_b);
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem_b, 8, unsized);
		t8 = pigen_data_type_unsigned_integer(&sem_b, width8);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(t8));
		source = pigen_source_add(&sources_b, "lower_boundary.pigen", text,
			strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name = (pigen_source_span){source, 19, 24}; /* "value" */
		sem_b.compilation_scope = pigen_scope_add(&sem_b,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem_b.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_b, sem_b.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem_b, sem_b.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem_b, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_b, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name, name, &signal_symbol, NULL) == PIGEN_DECLARE_OK);
		signal = pigen_signal_add(&sem_b, (pigen_syntax_id){2}, module,
			signal_symbol, t8, pigen_semantic_scalar_shape(&sem_b),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_ABSTRACT,
			PIGEN_SEMANTIC_INPUT, name);
		REQUIRE(signal.index != PIGEN_INVALID_ID);

		/* Owner facts the contract rides on (all green against the landed
		 * owners): the signal record reports ABSTRACT and the forced INPUT
		 * direction, the ABSTRACT descriptor is the only source mapping to
		 * the BOUNDARY realization with is_concrete=0 and context-dependent
		 * controls (valid_constant=-1, ready_constant=-1), and the BOUNDARY
		 * realization has ready_dependency PIGEN_TRANSFER_READY_EXTERNAL. */
		owner_signal = pigen_signal_get(&sem_b, signal);
		REQUIRE(owner_signal &&
			owner_signal->transfer_type == PIGEN_TRANSFER_TYPE_ABSTRACT &&
			owner_signal->direction == PIGEN_SEMANTIC_INPUT &&
			owner_signal->module.index == module.index);
		descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_ABSTRACT);
		REQUIRE(descriptor && !descriptor->is_concrete &&
			descriptor->valid_constant == -1 &&
			descriptor->ready_constant == -1);
		realization =
			pigen_transfer_realization_descriptor_get(
				PIGEN_TRANSFER_REALIZATION_BOUNDARY);
		REQUIRE(realization &&
			realization->ready_dependency ==
				PIGEN_TRANSFER_READY_EXTERNAL &&
			descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_BOUNDARY);
		/* The payload type witness: the already-implemented
		 * pigen_lower_rtl_type memo lowers the signal's 8-bit data type to
		 * the SAME record the declaration lowering must attach to the
		 * payload object (pointer equality in the shared RTL model). */
		lowered_t8 = pigen_lower_rtl_type(&lowering_b, t8);
		REQUIRE(!IS_INVALID_ID(lowered_t8));
		t8_record = pigen_rtl_type_get(&rtl_b, lowered_t8);
		REQUIRE(t8_record);

		/* Case 1: THREE-PORT SHAPE. The declaration lowering reports
		 * success and the endpoints record for the signal holds distinct
		 * payload (a resolvable RTL object), valid and ready (resolvable
		 * RTL expressions), with the input side exposing the SAME three
		 * objects. The first `rc == 0` passes against the landed
		 * lowering, which reports success for this module. */
		rc = pigen_lower_rtl_module_declarations(&lowering_b, module);
		REQUIRE(rc == 0); /* Green: the landed lowering reports success
		 * and publishes the endpoints record for the signal. */
		REQUIRE(lowering_b.lowered_endpoint_count > signal.index);
		endpoints = &lowering_b.lowered_endpoints[signal.index];
		REQUIRE(!IS_INVALID_ID(endpoints->payload));
		REQUIRE(!IS_INVALID_ID(endpoints->valid));
		REQUIRE(!IS_INVALID_ID(endpoints->ready));
		REQUIRE(!IS_INVALID_ID(endpoints->input_payload));
		REQUIRE(!IS_INVALID_ID(endpoints->input_valid));
		REQUIRE(!IS_INVALID_ID(endpoints->input_ready));
		payload_obj = pigen_rtl_object_get(&rtl_b, endpoints->payload);
		REQUIRE(payload_obj);
		valid_re = pigen_rtl_expr_get(&rtl_b, endpoints->valid);
		REQUIRE(valid_re);
		ready_re = pigen_rtl_expr_get(&rtl_b, endpoints->ready);
		REQUIRE(ready_re);
		/* The three ports are distinct objects in the RTL model: the
		 * payload object and the two control expression records are
		 * separate arena entries. The payload is a VARIABLE object of the
		 * signal's semantic direction, not a MEMORY or internal object: a
		 * BOUNDARY payload published as a MEMORY-kind object, or with an
		 * INTERNAL direction, is caught here. */
		distinct = payload_obj->kind == PIGEN_RTL_OBJECT_VARIABLE &&
			payload_obj->direction == PIGEN_SEMANTIC_INPUT &&
			!IS_INVALID_ID(payload_obj->type) &&
			pigen_rtl_type_get(&rtl_b, payload_obj->type) &&
			valid_re->kind != PIGEN_RTL_EXPR_INVALID &&
			ready_re->kind != PIGEN_RTL_EXPR_INVALID &&
			endpoints->valid.index != endpoints->ready.index;
		REQUIRE(distinct); /* Green: the landed lowering publishes distinct objects. */
		/* The payload type is EXACTLY the signal's lowered 8-bit data
		 * type - the SAME record the pigen_lower_rtl_type memo yields in
		 * the shared model (pointer equality): a wrong-but-resolvable
		 * payload type is caught here. */
		payload_shape = payload_obj &&
			pigen_rtl_type_get(&rtl_b, payload_obj->type) == t8_record;
		REQUIRE(payload_shape);
		/* Input side: the declaration exposes the SAME three objects on
		 * its input side - the same payload object, the same valid and
		 * ready expressions (the input-side fields are object ids, so
		 * they carry the same object/expression arena indices). */
		REQUIRE(endpoints->input_payload.index == endpoints->payload.index);
		input_obj = pigen_rtl_object_get(&rtl_b, endpoints->input_payload);
		REQUIRE(input_obj);
		REQUIRE(input_obj->kind != PIGEN_RTL_OBJECT_KIND_INVALID);
		/* The input-side valid/ready fields carry resolvable records:
		 * they resolve in the RTL model to the SAME records the
		 * declaration's valid/ready expression ids resolve to (same
		 * arena index, so the same record). */
		input_payload_obj = endpoints->input_payload;
		input_ready_obj = endpoints->input_ready;
		input_valid_expr = (pigen_rtl_expr_id){endpoints->valid.index};
		REQUIRE(pigen_rtl_expr_get(&rtl_b, input_valid_expr) == valid_re);
		REQUIRE(pigen_rtl_object_get(&rtl_b, input_payload_obj) == payload_obj);
		REQUIRE(pigen_rtl_object_get(&rtl_b, input_ready_obj));

		/* Case 2: CONTEXT-DEPENDENT CONTROLS. The boundary signal has NO
		 * constant controls: the valid and ready expression records must
		 * NOT be the owner-published constant expression for a 1-bit
		 * constant. The witness is lowered through
		 * pigen_const_expr_intern_integer + pigen_lower_rtl_expression on
		 * a SEPARATE lowering/model so its record (an
		 * PIGEN_RTL_EXPR_INTEGER with the 1-bit type) is the constant,
		 * independent of the boundary model. The constant-expression-
		 * identity contract belongs to section (9); no value is pinned
		 * here. Green: the landed lowering publishes the records. */
		{
			pigen_source_manager sources_c = {0};
			pigen_semantic_model sem_c;
			pigen_rtl_model rtl_c = {0};
			pigen_rtl_lowering lowering_c;
			pigen_data_type_id unsized_c;
			pigen_data_type_id t1bit;
			pigen_const_expr_id one1bit;
			pigen_rtl_expr_id const1;
			const pigen_rtl_expr *const_re;

			pigen_semantic_init(&sem_c, &sources_c);
			pigen_rtl_lowering_init(&lowering_c, &sem_c, &rtl_c);
			unsized_c = pigen_data_type_unsized_integer(&sem_c);
			t1bit = pigen_data_type_sized_logic(&sem_c, 1, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(unsized_c) && !IS_INVALID_ID(t1bit));
			one1bit = pigen_const_expr_intern_integer(&sem_c, 1, t1bit);
			REQUIRE(!IS_INVALID_ID(one1bit));
			const1 = pigen_lower_rtl_expression(&lowering_c, one1bit);
			REQUIRE(!IS_INVALID_ID(const1)); /* Green: the landed lowering
			 * resolves the valid 1-bit constant. */
			const_re = pigen_rtl_expr_get(&rtl_c, const1);
			REQUIRE(const_re && const_re->kind == PIGEN_RTL_EXPR_INTEGER);

			/* The boundary valid/ready records are NOT that constant:
			 * neither by id (the boundary model is a separate arena, so
			 * a shortcut that republished the constant's own record into
			 * the boundary model at the same index is caught here) nor by
			 * the record content the constant witness carries (kind
			 * INTEGER, value 1, 1-bit type). */
			not_constant = valid_re && ready_re;
			if (not_constant) {
				not_constant =
					!(valid_re->kind == const_re->kind &&
					valid_re->value == const_re->value &&
					valid_re->literal_bit_count ==
						const_re->literal_bit_count &&
					valid_re->literal_negative ==
						const_re->literal_negative &&
					valid_re->child_count == const_re->child_count);
			}
			if (not_constant) {
				not_constant =
					!(ready_re->kind == const_re->kind &&
					ready_re->value == const_re->value &&
					ready_re->literal_bit_count ==
						const_re->literal_bit_count &&
					ready_re->literal_negative ==
						const_re->literal_negative &&
					ready_re->child_count == const_re->child_count);
			}
			not_constant = not_constant &&
				endpoints->valid.index != const1.index &&
				endpoints->ready.index != const1.index;
			REQUIRE(not_constant); /* Green: the landed lowering publishes
			 * context-dependent controls, not the constant records. */
			pigen_rtl_lowering_free(&lowering_c);
			pigen_free_rtl_model(&rtl_c);
			pigen_free_semantic_model(&sem_c);
			pigen_free_sources(&sources_c);
		}

		/* Case 3: MEMO/STABILITY. A second call on the SAME lowering is
		 * idempotent: no duplicate objects/expressions, the endpoints map
		 * count is unchanged and the populated record is byte-identical.
		 * Green: the landed lowering publishes the records. */
		object_count_before = rtl_b.object_count;
		expression_count_before = rtl_b.expression_count;
		endpoint_map_before = lowering_b.lowered_endpoint_count;
		record_before = lowering_b.lowered_endpoints[signal.index];
		rc = pigen_lower_rtl_module_declarations(&lowering_b, module);
		REQUIRE(rc == 0); /* Green: the landed lowering reports success again. */
		endpoints_again = &lowering_b.lowered_endpoints[signal.index];
		record_again = *endpoints_again;
		stable = rtl_b.object_count == object_count_before &&
			rtl_b.expression_count == expression_count_before &&
			lowering_b.lowered_endpoint_count == endpoint_map_before &&
			(record_again.payload.index == record_before.payload.index) &&
			(record_again.valid.index == record_before.valid.index) &&
			(record_again.ready.index == record_before.ready.index) &&
			(record_again.input_payload.index ==
				record_before.input_payload.index) &&
			(record_again.input_valid.index ==
				record_before.input_valid.index) &&
			(record_again.input_ready.index ==
				record_before.input_ready.index);
		REQUIRE(stable); /* Green: the repeat call republishes nothing new. */

		/* A second independently built model (identical construction
		 * sequence, fresh managers) yields identical endpoint ids at
		 * equal offsets: the endpoints record of the second build's
		 * signal equals the first build's record at equal arena offsets,
		 * so the per-signal endpoint shape is deterministic. The second
		 * build frees its own models before the shared tail. */
		{
			pigen_source_manager sources_d = {0};
			pigen_semantic_model sem_d;
			pigen_rtl_model rtl_d = {0};
			pigen_rtl_lowering lowering_d;
			pigen_data_type_id unsized_d;
			pigen_data_type_id t8_d;
			pigen_const_expr_id width8_d;
			pigen_source_id source_d;
			pigen_source_span whole_d;
			pigen_source_span name_d;
			pigen_scope_id module_scope_d;
			pigen_symbol_id module_symbol_d;
			pigen_symbol_id signal_symbol_d;
			pigen_module_id module_d;
			pigen_signal_id signal_d;
			const pigen_rtl_signal_endpoints *ep1;
			const pigen_rtl_signal_endpoints *ep2;
			int deterministic;

			pigen_semantic_init(&sem_d, &sources_d);
			pigen_rtl_lowering_init(&lowering_d, &sem_d, &rtl_d);
			unsized_d = pigen_data_type_unsized_integer(&sem_d);
			width8_d = pigen_const_expr_intern_integer(&sem_d, 8, unsized_d);
			t8_d = pigen_data_type_unsigned_integer(&sem_d, width8_d);
			REQUIRE(!IS_INVALID_ID(unsized_d) && !IS_INVALID_ID(width8_d) &&
				!IS_INVALID_ID(t8_d));
			source_d = pigen_source_add(&sources_d, "lower_boundary.pigen",
				text, strlen(text));
			REQUIRE(source_d.index != PIGEN_INVALID_ID);
			whole_d = (pigen_source_span){source_d, 0, strlen(text)};
			name_d = (pigen_source_span){source_d, 19, 24}; /* "value" */
			sem_d.compilation_scope = pigen_scope_add(&sem_d,
				(pigen_scope_id){PIGEN_INVALID_ID},
				(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID},
					0, 0});
			REQUIRE(sem_d.compilation_scope.index != PIGEN_INVALID_ID);
			REQUIRE(pigen_symbol_declare(&sem_d, sem_d.compilation_scope,
				PIGEN_SYMBOL_MODULE,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				whole_d, whole_d, &module_symbol_d, NULL) ==
				PIGEN_DECLARE_OK);
			module_scope_d =
				pigen_scope_add(&sem_d, sem_d.compilation_scope, whole_d);
			REQUIRE(module_scope_d.index != PIGEN_INVALID_ID);
			module_d = pigen_module_add(&sem_d, (pigen_syntax_id){1},
				module_symbol_d, module_scope_d, whole_d);
			REQUIRE(module_d.index != PIGEN_INVALID_ID);
			REQUIRE(pigen_symbol_declare(&sem_d, module_scope_d,
				PIGEN_SYMBOL_SIGNAL, t8_d, name_d, name_d,
				&signal_symbol_d, NULL) == PIGEN_DECLARE_OK);
			signal_d = pigen_signal_add(&sem_d, (pigen_syntax_id){2},
				module_d, signal_symbol_d, t8_d,
				pigen_semantic_scalar_shape(&sem_d),
				(pigen_expr_id){PIGEN_INVALID_ID},
				PIGEN_TRANSFER_TYPE_ABSTRACT, PIGEN_SEMANTIC_INPUT, name_d);
			REQUIRE(signal_d.index != PIGEN_INVALID_ID);
			REQUIRE(pigen_lower_rtl_module_declarations(&lowering_d,
				module_d) == 0); /* Green: the landed lowering reports success. */
			REQUIRE(lowering_d.lowered_endpoint_count > signal_d.index);
			ep1 = &lowering_b.lowered_endpoints[signal.index];
			ep2 = &lowering_d.lowered_endpoints[signal_d.index];
			deterministic = ep1 && ep2 &&
				ep1->payload.index == ep2->payload.index &&
				ep1->valid.index == ep2->valid.index &&
				ep1->ready.index == ep2->ready.index &&
				ep1->input_payload.index == ep2->input_payload.index &&
				ep1->input_valid.index == ep2->input_valid.index &&
				ep1->input_ready.index == ep2->input_ready.index;
			REQUIRE(deterministic); /* Green: both builds publish identical
			 * endpoints records at equal arena offsets. */
			pigen_rtl_lowering_free(&lowering_d);
			pigen_free_rtl_model(&rtl_d);
			pigen_free_semantic_model(&sem_d);
			pigen_free_sources(&sources_d);
		}

		/* Case 4: FAILURE REPORT. A genuinely DIFFERENT failing module,
		 * not a repeat of Case 3's call: a second module in the SAME
		 * lowering (its own scope, its own source file, one ABSTRACT
		 * INPUT signal) whose signal data type is a NEGATIVE exact
		 * integer - the stable, owner-constructible failure witness. The
		 * type is constructible through the owner APIs
		 * (pigen_integer_negate + pigen_data_type_exact_integer) yet
		 * pigen_lower_rtl_type refuses it (its unsigned width is zero;
		 * pinned by section (5)'s negative-exact-integer boundary), and
		 * the declaration lowering must lower the signal's data type to
		 * type the payload object - so this module fails deterministically
		 * regardless of the adapter table's state: the SAME
		 * implementation that makes Case 1 and Case 3 green makes this
		 * call return -1. The transient unimplemented-adapter path is
		 * deliberately NOT used as the frozen failure witness: it cannot
		 * by itself justify a frozen failure assertion once every
		 * promised adapter is implemented. The contract pins only the
		 * reported -1. */
		{
			const char *text2 =
				"module fail : input value : abstract ;\n";
			pigen_integer_id neg_value;
			pigen_data_type_id t_neg;
			pigen_source_id source2;
			pigen_source_span whole2;
			pigen_source_span name2;
			pigen_scope_id module_scope2;
			pigen_symbol_id module_symbol2;
			pigen_symbol_id signal_symbol2;
			pigen_module_id module2;
			pigen_signal_id signal2;

			/* The stable failure witness: intern the exact value 5, negate
			 * it, and build the exact-integer data type carrying the
			 * negative value. The type exists in the semantic owner but
			 * is not lowerable: pigen_lower_rtl_type reports the
			 * sentinel for it (its unsigned width is zero), which is the
			 * pinned negative-exact-integer boundary of section (5). */
			neg_value = pigen_integer_intern_u64(&sem_b, 5);
			REQUIRE(!IS_INVALID_ID(neg_value));
			neg_value = pigen_integer_negate(&sem_b, neg_value);
			REQUIRE(!IS_INVALID_ID(neg_value));
			t_neg = pigen_data_type_exact_integer(&sem_b, neg_value);
			REQUIRE(!IS_INVALID_ID(t_neg));
			REQUIRE(pigen_lower_rtl_type(&lowering_b, t_neg).index ==
				PIGEN_INVALID_ID); /* guard: the landed type lowering
			 * refuses the negative exact integer */

			/* The second module in the SAME lowering: its own source
			 * file, module symbol + scope in the compilation scope, and
			 * one ABSTRACT INPUT signal of the unlowerable type. */
			source2 = pigen_source_add(&sources_b, "lower_boundary_fail.pigen",
				text2, strlen(text2));
			REQUIRE(source2.index != PIGEN_INVALID_ID);
			whole2 = (pigen_source_span){source2, 0, strlen(text2)};
			name2 = (pigen_source_span){source2, 20, 25}; /* "value" */
			REQUIRE(pigen_symbol_declare(&sem_b, sem_b.compilation_scope,
				PIGEN_SYMBOL_MODULE,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				whole2, whole2, &module_symbol2, NULL) == PIGEN_DECLARE_OK);
			module_scope2 =
				pigen_scope_add(&sem_b, sem_b.compilation_scope, whole2);
			REQUIRE(module_scope2.index != PIGEN_INVALID_ID);
			module2 = pigen_module_add(&sem_b, (pigen_syntax_id){3},
				module_symbol2, module_scope2, whole2);
			REQUIRE(module2.index != PIGEN_INVALID_ID);
			REQUIRE(pigen_symbol_declare(&sem_b, module_scope2,
				PIGEN_SYMBOL_SIGNAL, t_neg, name2, name2,
				&signal_symbol2, NULL) == PIGEN_DECLARE_OK);
			signal2 = pigen_signal_add(&sem_b, (pigen_syntax_id){4},
				module2, signal_symbol2, t_neg,
				pigen_semantic_scalar_shape(&sem_b),
				(pigen_expr_id){PIGEN_INVALID_ID},
				PIGEN_TRANSFER_TYPE_ABSTRACT, PIGEN_SEMANTIC_INPUT, name2);
			REQUIRE(signal2.index != PIGEN_INVALID_ID);

			/* The different failing module reports the error: the
			 * unlowerable signal data type fails the declaration
			 * lowering, which returns -1. Green: the landed lowering
			 * refuses the negative exact integer deterministically, and
			 * this assert pins the stable failure witness. */
			rc = pigen_lower_rtl_module_declarations(&lowering_b, module2);
			REQUIRE(rc == -1);
		}

		pigen_rtl_lowering_free(&lowering_b);
		pigen_free_rtl_model(&rtl_b);
		pigen_free_semantic_model(&sem_b);
		pigen_free_sources(&sources_b);
	}

	/* (9) Net and variable realizations: the COMBINATIONAL_NET /
	 * PROCEDURAL_VARIABLE PAYLOAD-SHAPE contract for
	 * pigen_lower_rtl_module_declarations (Task 6, "combinational net /
	 * procedural variable -> payload plus constants"). One module holds TWO
	 * INTERNAL signals of the SAME 8-bit unsigned data type: a
	 * PIGEN_TRANSFER_TYPE_WIRE (COMBINATIONAL_NET) and a
	 * PIGEN_TRANSFER_TYPE_REG (PROCEDURAL_VARIABLE), built through the owner
	 * APIs exactly as section (8)'s landed construction pattern: a source
	 * file, the compilation scope, a module symbol + pigen_module_add, a
	 * PIGEN_SYMBOL_SIGNAL declaration per signal (matching data type and
	 * declaration span) and pigen_signal_add with PIGEN_SEMANTIC_INTERNAL -
	 * permitted for concrete types (the owner forces INPUT only for
	 * non-concrete types and rejects NULL transfer-type descriptors). Both
	 * descriptors are owner facts this contract rides on (green against the
	 * landed owners): WIRE is concrete with realization COMBINATIONAL_NET,
	 * and REG is concrete with realization PROCEDURAL_VARIABLE.
	 *   Case 1: PAYLOAD SHAPE. The call reports success (green against the
	 *     landed lowering, not a compile or harness error) and the
	 *     endpoints map grows past BOTH signal indices. For EACH signal,
	 *     the endpoints record is published in the model and every
	 *     identity in it is resolvable: the endpoints payload is a valid id resolving to a
	 *     pigen_rtl_object of kind PIGEN_RTL_OBJECT_VARIABLE whose type is
	 *     the lowered 8-bit type - the SAME record the landed
	 *     pigen_lower_rtl_type memo yields for the signal's data type - and
	 *     whose direction equals the semantic signal's direction.
	 *     The payload-vs-control distinction is namespace-level: a
	 *     pigen_rtl_object_id can never equal a pigen_rtl_expr_id
	 *     (independent struct-wrapped uint32_t arena counters with
	 *     separate arenas in the model), so same-namespace distinctness is
	 *     pinned instead: the two signals yield two DISTINCT payload
	 *     objects in the object arena (an implementation sharing one
	 *     object between the net and the variable is caught), and each of
	 *     the four valid/ready control identities (wire valid, wire ready,
	 *     reg valid, reg ready) resolves in the expression arena to a
	 *     PIGEN_RTL_EXPR_INTEGER record whose literal is exactly one bit
	 *     (literal_bit_count == 1, literal_negative == 0) - which holds
	 *     for every interning variant, shared or per-signal records.
	 * The descriptor constant VALUES (WIRE ready 0, REG ready 1, valid 1)
	 * and the arena count deltas belong to the sibling sections (10) and
	 * (11); no constant value or arena count delta is pinned here.
	 * Green: every assert in this section passes against the landed Task 6
	 * net/variable declaration lowering; the first control-record assert,
	 * marked below, holds because the implementation publishes the
	 * one-bit valid/ready constant expressions for both signals, so the
	 * four expressions resolve to PIGEN_RTL_EXPR_INTEGER records. Every
	 * later assert in this section holds as well. No stale red
	 * markers remain in this file. */
	SECTION("t6-net-variable-payload") {
		const char *text =
			"module netvar : wire a ; reg b ;\n";
		pigen_source_manager sources_e = {0};
		pigen_semantic_model sem_e;
		pigen_rtl_model rtl_e = {0};
		pigen_rtl_lowering lowering_e;
		pigen_data_type_id unsized;
		pigen_data_type_id t8;
		pigen_const_expr_id width8;
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name_a;
		pigen_source_span name_b;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id wire_symbol;
		pigen_symbol_id reg_symbol;
		pigen_module_id module;
		pigen_signal_id wire;
		pigen_signal_id reg;
		const pigen_semantic_signal *wire_owner;
		const pigen_semantic_signal *reg_owner;
		const pigen_transfer_type_descriptor *wire_descriptor;
		const pigen_transfer_type_descriptor *reg_descriptor;
		pigen_rtl_type_id lowered_t8;
		const pigen_rtl_type *t8_record;
		const pigen_rtl_type *wire_payload_type;
		const pigen_rtl_type *reg_payload_type;
		const pigen_rtl_signal_endpoints *wire_endpoints;
		const pigen_rtl_signal_endpoints *reg_endpoints;
		const pigen_rtl_object *wire_payload_obj;
		const pigen_rtl_object *reg_payload_obj;
		const pigen_rtl_expr *wire_valid_re;
		const pigen_rtl_expr *wire_ready_re;
		const pigen_rtl_expr *reg_valid_re;
		const pigen_rtl_expr *reg_ready_re;
		int rc;
		int payload_wire;
		int payload_reg;
		int distinct;
		int control_witness;

		/* Build the net/variable module through the owner APIs (section
		 * (8)'s landed construction pattern, two INTERNAL signals of the
		 * same 8-bit type): the type first, then a source file, the
		 * compilation scope, a module in it, and the two declared signals.
		 * The module symbol's declaration spans the whole file (as
		 * module_add requires), the module scope uses the same span, and
		 * each signal symbol's name span is contained in and its
		 * declaration span equal to the span signal_add checks. */
		pigen_semantic_init(&sem_e, &sources_e);
		pigen_rtl_lowering_init(&lowering_e, &sem_e, &rtl_e);
		unsized = pigen_data_type_unsized_integer(&sem_e);
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem_e, 8, unsized);
		t8 = pigen_data_type_unsigned_integer(&sem_e, width8);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(t8));
		source = pigen_source_add(&sources_e, "lower_netvar.pigen", text,
			strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name_a = (pigen_source_span){source, 21, 22}; /* "a" */
		name_b = (pigen_source_span){source, 29, 30}; /* "b" */
		sem_e.compilation_scope = pigen_scope_add(&sem_e,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem_e.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_e, sem_e.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem_e, sem_e.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem_e, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_e, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_a, name_a, &wire_symbol, NULL) == PIGEN_DECLARE_OK);
		wire = pigen_signal_add(&sem_e, (pigen_syntax_id){2}, module,
			wire_symbol, t8, pigen_semantic_scalar_shape(&sem_e),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_WIRE,
			PIGEN_SEMANTIC_INTERNAL, name_a);
		REQUIRE(wire.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_e, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_b, name_b, &reg_symbol, NULL) == PIGEN_DECLARE_OK);
		reg = pigen_signal_add(&sem_e, (pigen_syntax_id){3}, module,
			reg_symbol, t8, pigen_semantic_scalar_shape(&sem_e),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_REG,
			PIGEN_SEMANTIC_INTERNAL, name_b);
		REQUIRE(reg.index != PIGEN_INVALID_ID);

		/* Owner facts the contract rides on (all green against the landed
		 * owners): both signal records report their transfer type, the
		 * INTERNAL direction and the module; WIRE and REG are concrete
		 * descriptors mapped to the COMBINATIONAL_NET and
		 * PROCEDURAL_VARIABLE realizations; and the already-implemented
		 * pigen_lower_rtl_type memo resolves the 8-bit data type to a
		 * record in the RTL model. */
		wire_owner = pigen_signal_get(&sem_e, wire);
		REQUIRE(wire_owner &&
			wire_owner->transfer_type == PIGEN_TRANSFER_TYPE_WIRE &&
			wire_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			wire_owner->module.index == module.index);
		reg_owner = pigen_signal_get(&sem_e, reg);
		REQUIRE(reg_owner &&
			reg_owner->transfer_type == PIGEN_TRANSFER_TYPE_REG &&
			reg_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			reg_owner->module.index == module.index);
		wire_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_WIRE);
		REQUIRE(wire_descriptor && wire_descriptor->is_concrete &&
			wire_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET);
		reg_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_REG);
		REQUIRE(reg_descriptor && reg_descriptor->is_concrete &&
			reg_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE);
		lowered_t8 = pigen_lower_rtl_type(&lowering_e, t8);
		REQUIRE(!IS_INVALID_ID(lowered_t8));
		t8_record = pigen_rtl_type_get(&rtl_e, lowered_t8);
		REQUIRE(t8_record);

		/* Case 1: PAYLOAD SHAPE. The declaration lowering reports success
		 * and the endpoints map covers BOTH signals. The first
		 * `rc == 0` passes against the landed lowering, which reports
		 * success for this module. */
		rc = pigen_lower_rtl_module_declarations(&lowering_e, module);
		REQUIRE(rc == 0); /* Green: the landed lowering reports success
		 * and publishes the endpoints records for both signals. */
		REQUIRE(lowering_e.lowered_endpoint_count >
			(wire.index > reg.index ? wire.index : reg.index));

		/* For EACH signal the payload is a resolvable VARIABLE object:
		 * kind PIGEN_RTL_OBJECT_VARIABLE, type the lowered 8-bit type -
		 * the SAME record the pigen_lower_rtl_type memo yielded - and
		 * direction the semantic signal's direction. */
		wire_endpoints = &lowering_e.lowered_endpoints[wire.index];
		reg_endpoints = &lowering_e.lowered_endpoints[reg.index];
		REQUIRE(!IS_INVALID_ID(wire_endpoints->payload));
		REQUIRE(!IS_INVALID_ID(reg_endpoints->payload));
		wire_payload_obj = pigen_rtl_object_get(&rtl_e,
			wire_endpoints->payload);
		reg_payload_obj = pigen_rtl_object_get(&rtl_e, reg_endpoints->payload);
		wire_payload_type = pigen_rtl_type_get(&rtl_e,
			wire_payload_obj ? wire_payload_obj->type :
			(pigen_rtl_type_id){PIGEN_INVALID_ID});
		reg_payload_type = pigen_rtl_type_get(&rtl_e,
			reg_payload_obj ? reg_payload_obj->type :
			(pigen_rtl_type_id){PIGEN_INVALID_ID});
		payload_wire = wire_payload_obj &&
			wire_payload_obj->kind == PIGEN_RTL_OBJECT_VARIABLE &&
			wire_payload_type == t8_record &&
			wire_payload_obj->direction == PIGEN_SEMANTIC_INTERNAL;
		REQUIRE(payload_wire); /* Green: the landed lowering publishes the wire payload. */
		payload_reg = reg_payload_obj &&
			reg_payload_obj->kind == PIGEN_RTL_OBJECT_VARIABLE &&
			reg_payload_type == t8_record &&
			reg_payload_obj->direction == PIGEN_SEMANTIC_INTERNAL;
		REQUIRE(payload_reg); /* Green: the landed lowering publishes the reg payload. */

		/* Same-namespace object-arena distinctness: the two signals
		 * yield two DISTINCT payload objects - an implementation that
		 * shares one object between the net and the variable is caught.
		 * The payload-vs-control distinction is namespace-level (a
		 * pigen_rtl_object_id can never equal a pigen_rtl_expr_id), so
		 * no cross-arena index comparison is pinned here: on this fresh
		 * model both arenas are empty before the declaration call (the
		 * pre-call type memo publishes a type record, no object and no
		 * expression), so the first payload object (index 0) and the
		 * first control expression (index 0) MUST collide numerically in
		 * any correct implementation. */
		distinct = wire_endpoints->payload.index !=
				reg_endpoints->payload.index;
		REQUIRE(distinct); /* Green: the landed lowering publishes distinct objects. */

		/* Same-namespace expression-arena witness for the
		 * control-distinct intent: EACH of the four valid/ready control
		 * identities resolves to a non-NULL PIGEN_RTL_EXPR_INTEGER record
		 * whose literal is exactly one bit (literal_bit_count == 1,
		 * literal_negative == 0). Green: the landed lowering publishes
		 * the one-bit valid/ready constant expressions for both signals,
		 * so the four expressions resolve in the expression arena and the
		 * assert holds for every interning variant, shared or per-signal
		 * records. The descriptor constant VALUES (WIRE ready
		 * 0, REG ready 1, both valid 1) are pinned by the sibling
		 * constant-controls section (10), not here; the four records
		 * hold for every interning variant. */
		wire_valid_re = pigen_rtl_expr_get(&rtl_e, wire_endpoints->valid);
		wire_ready_re = pigen_rtl_expr_get(&rtl_e, wire_endpoints->ready);
		reg_valid_re = pigen_rtl_expr_get(&rtl_e, reg_endpoints->valid);
		reg_ready_re = pigen_rtl_expr_get(&rtl_e, reg_endpoints->ready);
		control_witness = wire_valid_re && wire_ready_re &&
			reg_valid_re && reg_ready_re &&
			wire_valid_re->kind == PIGEN_RTL_EXPR_INTEGER &&
			wire_ready_re->kind == PIGEN_RTL_EXPR_INTEGER &&
			reg_valid_re->kind == PIGEN_RTL_EXPR_INTEGER &&
			reg_ready_re->kind == PIGEN_RTL_EXPR_INTEGER &&
			wire_valid_re->literal_bit_count == 1 &&
			wire_ready_re->literal_bit_count == 1 &&
			reg_valid_re->literal_bit_count == 1 &&
			reg_ready_re->literal_bit_count == 1 &&
			wire_valid_re->literal_negative == 0 &&
			wire_ready_re->literal_negative == 0 &&
			reg_valid_re->literal_negative == 0 &&
			reg_ready_re->literal_negative == 0;
		REQUIRE(control_witness); /* Green: the landed lowering publishes
		 * the one-bit valid/ready constant records for both signals. */

		pigen_rtl_lowering_free(&lowering_e);
		pigen_free_rtl_model(&rtl_e);
		pigen_free_semantic_model(&sem_e);
		pigen_free_sources(&sources_e);
	}

	/* (10) Net and variable constant controls: the COMBINATIONAL_NET /
	 * PROCEDURAL_VARIABLE CONSTANT-CONTROLS contract for
	 * pigen_lower_rtl_module_declarations (Task 6, "combinational net /
	 * procedural variable -> payload plus constants"). Sibling of section
	 * (9), which pins the payload shape: one module holds TWO INTERNAL
	 * signals of the SAME 8-bit unsigned data type, a
	 * PIGEN_TRANSFER_TYPE_WIRE (COMBINATIONAL_NET) and a
	 * PIGEN_TRANSFER_TYPE_REG (PROCEDURAL_VARIABLE), built through the
	 * owner APIs exactly as section (8)'s landed construction pattern: a
	 * source file, the compilation scope, a module symbol +
	 * pigen_module_add, a PIGEN_SYMBOL_SIGNAL declaration per signal
	 * (matching data type and declaration span) and pigen_signal_add with
	 * PIGEN_SEMANTIC_INTERNAL. The descriptor constants this contract
	 * rides on are owner facts (green against the landed owners): WIRE is
	 * valid_constant=1 / ready_constant=0, REG is valid_constant=1 /
	 * ready_constant=1.
	 *   Case 1: CONSTANT CONTROLS. The call reports success (rc == 0),
	 *     green against the landed lowering (not a compile or harness
	 *     error). For
	 *     EACH signal, valid and ready are valid constant-expression
	 *     handles: pigen_rtl_expr_get resolves each to a record of kind
	 *     PIGEN_RTL_EXPR_INTEGER whose value carries the descriptor
	 *     constant (WIRE valid 1, WIRE ready 0, REG valid 1, REG ready
	 *     1) and whose literal is exactly one bit (literal_bit_count ==
	 *     1). The control carries NO published object: the endpoints
	 *     record has no object-id field for a control, and the object
	 *     arena publishes exactly one new object per signal - the
	 *     payload - so nothing extra is published for the controls.
	 *     WIRE and REG differ EXACTLY in the ready constant (0 vs 1)
	 *     while both carry the valid constant 1: the ready value is the
	 *     only differing field. The payload object kind and the full
	 *     per-signal count deltas belong to the sibling sections; only
	 *     the control records and the no-control-object delta are pinned
	 *     here.
	 * Green: every owner-construction and descriptor assert passes
	 * against the landed owners (including the already-implemented
	 * pigen_lower_rtl_type memo, which resolves the 8-bit type); the
	 * declaration lowering (Case 1's `rc == 0`) and every later
	 * assert in this section pass against the landed lowering.
	 * No stale red marker remains in this file. */
	SECTION("t6-net-variable-controls") {
		const char *text =
			"module netvar : wire a ; reg b ;\n";
		pigen_source_manager sources_f = {0};
		pigen_semantic_model sem_f;
		pigen_rtl_model rtl_f = {0};
		pigen_rtl_lowering lowering_f;
		pigen_data_type_id unsized;
		pigen_data_type_id t8;
		pigen_const_expr_id width8;
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name_a;
		pigen_source_span name_b;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id wire_symbol;
		pigen_symbol_id reg_symbol;
		pigen_module_id module;
		pigen_signal_id wire;
		pigen_signal_id reg;
		const pigen_semantic_signal *wire_owner;
		const pigen_semantic_signal *reg_owner;
		const pigen_transfer_type_descriptor *wire_descriptor;
		const pigen_transfer_type_descriptor *reg_descriptor;
		pigen_rtl_type_id lowered_t8;
		const pigen_rtl_type *t8_record;
		const pigen_rtl_signal_endpoints *wire_endpoints;
		const pigen_rtl_signal_endpoints *reg_endpoints;
		const pigen_rtl_expr *wire_valid_re;
		const pigen_rtl_expr *wire_ready_re;
		const pigen_rtl_expr *reg_valid_re;
		const pigen_rtl_expr *reg_ready_re;
		const pigen_rtl_type *one_bit;
		size_t object_count_before;
		size_t object_count_after;
		int rc;
		int controls_wire;
		int controls_reg;
		int no_object;
		int only_ready;

		/* Build the net/variable module through the owner APIs (section
		 * (8)'s landed construction pattern, two INTERNAL signals of the
		 * same 8-bit type): the type first, then a source file, the
		 * compilation scope, a module in it, and the two declared signals.
		 * The module symbol's declaration spans the whole file (as
		 * module_add requires), the module scope uses the same span, and
		 * each signal symbol's name span is contained in and its
		 * declaration span equal to the span signal_add checks. */
		pigen_semantic_init(&sem_f, &sources_f);
		pigen_rtl_lowering_init(&lowering_f, &sem_f, &rtl_f);
		unsized = pigen_data_type_unsized_integer(&sem_f);
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem_f, 8, unsized);
		t8 = pigen_data_type_unsigned_integer(&sem_f, width8);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(t8));
		source = pigen_source_add(&sources_f, "lower_netvar_const.pigen",
			text, strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name_a = (pigen_source_span){source, 21, 22}; /* "a" */
		name_b = (pigen_source_span){source, 29, 30}; /* "b" */
		sem_f.compilation_scope = pigen_scope_add(&sem_f,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem_f.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_f, sem_f.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem_f, sem_f.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem_f, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_f, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_a, name_a, &wire_symbol, NULL) == PIGEN_DECLARE_OK);
		wire = pigen_signal_add(&sem_f, (pigen_syntax_id){2}, module,
			wire_symbol, t8, pigen_semantic_scalar_shape(&sem_f),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_WIRE,
			PIGEN_SEMANTIC_INTERNAL, name_a);
		REQUIRE(wire.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_f, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_b, name_b, &reg_symbol, NULL) == PIGEN_DECLARE_OK);
		reg = pigen_signal_add(&sem_f, (pigen_syntax_id){3}, module,
			reg_symbol, t8, pigen_semantic_scalar_shape(&sem_f),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_REG,
			PIGEN_SEMANTIC_INTERNAL, name_b);
		REQUIRE(reg.index != PIGEN_INVALID_ID);

		/* Owner facts the contract rides on (all green against the landed
		 * owners): both signal records report their transfer type, the
		 * INTERNAL direction and the module; WIRE and REG are concrete
		 * descriptors mapped to the COMBINATIONAL_NET and
		 * PROCEDURAL_VARIABLE realizations with the descriptor constants
		 * this section pins (WIRE valid 1 / ready 0, REG valid 1 / ready
		 * 1); and the already-implemented pigen_lower_rtl_type memo
		 * resolves the 8-bit data type to a record in the RTL model. */
		wire_owner = pigen_signal_get(&sem_f, wire);
		REQUIRE(wire_owner &&
			wire_owner->transfer_type == PIGEN_TRANSFER_TYPE_WIRE &&
			wire_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			wire_owner->module.index == module.index);
		reg_owner = pigen_signal_get(&sem_f, reg);
		REQUIRE(reg_owner &&
			reg_owner->transfer_type == PIGEN_TRANSFER_TYPE_REG &&
			reg_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			reg_owner->module.index == module.index);
		wire_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_WIRE);
		REQUIRE(wire_descriptor && wire_descriptor->is_concrete &&
			wire_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET &&
			wire_descriptor->valid_constant == 1 &&
			wire_descriptor->ready_constant == 0);
		reg_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_REG);
		REQUIRE(reg_descriptor && reg_descriptor->is_concrete &&
			reg_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE &&
			reg_descriptor->valid_constant == 1 &&
			reg_descriptor->ready_constant == 1);
		lowered_t8 = pigen_lower_rtl_type(&lowering_f, t8);
		REQUIRE(!IS_INVALID_ID(lowered_t8));
		t8_record = pigen_rtl_type_get(&rtl_f, lowered_t8);
		REQUIRE(t8_record);

		/* The object arena is empty before the declaration call: the
		 * already-run type memo publishes type records, not objects, so
		 * every object the first successful call adds is attributable to
		 * the two declarations. */
		object_count_before = rtl_f.object_count;
		REQUIRE(object_count_before == 0);

		/* Case 1: CONSTANT CONTROLS. The declaration lowering reports
		 * success and the endpoints map covers BOTH signals. The first
		 * `rc == 0` is green against the landed lowering; the
		 * declaration lowering reports success. */
		rc = pigen_lower_rtl_module_declarations(&lowering_f, module);
		REQUIRE(rc == 0); /* Green against the landed lowering: the
		 * declaration lowering reports success. */
		REQUIRE(lowering_f.lowered_endpoint_count >
			(wire.index > reg.index ? wire.index : reg.index));

		/* For EACH signal, valid and ready are valid constant-expression
		 * handles resolving to a PIGEN_RTL_EXPR_INTEGER record whose
		 * value is the descriptor constant and whose literal is exactly
		 * one bit. The 1-bit type is witnessed through the landed type
		 * memo: a fresh 1-bit logic type resolves to a record of width
		 * 1, which is what a 1-bit control literal must carry. */
		one_bit = pigen_rtl_type_get(&rtl_f,
			pigen_lower_rtl_type(&lowering_f,
				pigen_data_type_sized_logic(&sem_f, 1,
					PIGEN_SIGN_UNSIGNED)));
		REQUIRE(one_bit && one_bit->width == 1);
		wire_endpoints = &lowering_f.lowered_endpoints[wire.index];
		reg_endpoints = &lowering_f.lowered_endpoints[reg.index];
		REQUIRE(!IS_INVALID_ID(wire_endpoints->valid));
		REQUIRE(!IS_INVALID_ID(wire_endpoints->ready));
		REQUIRE(!IS_INVALID_ID(reg_endpoints->valid));
		REQUIRE(!IS_INVALID_ID(reg_endpoints->ready));
		wire_valid_re = pigen_rtl_expr_get(&rtl_f, wire_endpoints->valid);
		wire_ready_re = pigen_rtl_expr_get(&rtl_f, wire_endpoints->ready);
		reg_valid_re = pigen_rtl_expr_get(&rtl_f, reg_endpoints->valid);
		reg_ready_re = pigen_rtl_expr_get(&rtl_f, reg_endpoints->ready);
		controls_wire = wire_valid_re && wire_ready_re &&
			wire_valid_re->kind == PIGEN_RTL_EXPR_INTEGER &&
			wire_ready_re->kind == PIGEN_RTL_EXPR_INTEGER &&
			wire_valid_re->value == 1 &&
			wire_ready_re->value == 0 &&
			wire_valid_re->literal_bit_count == 1 &&
			wire_ready_re->literal_bit_count == 1 &&
			wire_valid_re->literal_negative == 0 &&
			wire_ready_re->literal_negative == 0;
		REQUIRE(controls_wire); /* Green: the landed lowering populates
		 * both WIRE control records. */
		controls_reg = reg_valid_re && reg_ready_re &&
			reg_valid_re->kind == PIGEN_RTL_EXPR_INTEGER &&
			reg_ready_re->kind == PIGEN_RTL_EXPR_INTEGER &&
			reg_valid_re->value == 1 &&
			reg_ready_re->value == 1 &&
			reg_valid_re->literal_bit_count == 1 &&
			reg_ready_re->literal_bit_count == 1 &&
			reg_valid_re->literal_negative == 0 &&
			reg_ready_re->literal_negative == 0;
		REQUIRE(controls_reg); /* Green: the landed lowering populates the REG controls. */

		/* The control carries NO published object: the endpoints record
		 * has no object-id field for a control, so the witness is the
		 * object arena - the first successful declaration call publishes
		 * exactly one new object per signal (the payload) and nothing
		 * for the controls. An implementation that also publishes an
		 * object for a constant control (or shares one payload object)
		 * is caught here. */
		object_count_after = rtl_f.object_count;
		no_object = (object_count_after - object_count_before) == 2;
		REQUIRE(no_object); /* Green: the landed lowering publishes no control object. */

		/* WIRE and REG differ EXACTLY in the ready constant (0 vs 1)
		 * while both carry the valid constant 1: the ready value is the
		 * only differing field of the four control records. */
		only_ready = wire_valid_re && wire_ready_re && reg_valid_re &&
			reg_ready_re &&
			wire_valid_re->kind == reg_valid_re->kind &&
			wire_valid_re->value == reg_valid_re->value &&
			wire_valid_re->literal_bit_count ==
				reg_valid_re->literal_bit_count &&
			wire_valid_re->literal_negative ==
				reg_valid_re->literal_negative &&
			wire_ready_re->kind == reg_ready_re->kind &&
			wire_ready_re->value != reg_ready_re->value &&
			wire_ready_re->literal_bit_count ==
				reg_ready_re->literal_bit_count &&
			wire_ready_re->literal_negative ==
				reg_ready_re->literal_negative;
		REQUIRE(only_ready); /* Green: WIRE and REG differ only in the ready constant. */

		pigen_rtl_lowering_free(&lowering_f);
		pigen_free_rtl_model(&rtl_f);
		pigen_free_semantic_model(&sem_f);
		pigen_free_sources(&sources_f);
	}

	/* (11) Net and variable counts and idempotence: the
	 * COMBINATIONAL_NET / PROCEDURAL_VARIABLE NO-STORAGE COUNT-DELTA and
	 * IDEMPOTENCE contract for pigen_lower_rtl_module_declarations (Task 6,
	 * "combinational net / procedural variable -> payload plus constants").
	 * Sibling of sections (9) (payload shape) and (10) (constant controls):
	 * one module holds TWO INTERNAL signals of the SAME 8-bit unsigned data
	 * type, a PIGEN_TRANSFER_TYPE_WIRE (COMBINATIONAL_NET) and a
	 * PIGEN_TRANSFER_TYPE_REG (PROCEDURAL_VARIABLE), built through the owner
	 * APIs exactly as section (8)'s landed construction pattern: a source
	 * file, the compilation scope, a module symbol + pigen_module_add, a
	 * PIGEN_SYMBOL_SIGNAL declaration per signal (matching data type and
	 * declaration span) and pigen_signal_add with PIGEN_SEMANTIC_INTERNAL.
	 *   Case 1: COUNT DELTAS. The call reports success (rc == 0), green
	 *     against the landed lowering (not a compile or harness error).
	 *     The object-arena delta
	 *     for the whole module is EXACTLY two - one payload object per
	 *     signal - because a constant control carries no published object;
	 *     the expression-arena delta is AT MOST two per signal (four for the
	 *     module) and at least the payload objects: the two valid constants
	 *     (both 1) may share a single interned 1-bit record when the
	 *     lowering interns by value, and the ready constants (WIRE 0, REG 1)
	 *     differ, so four is the upper bound and two the lower bound; no
	 *     pigen_rtl_instance is published (instance-arena delta exactly
	 *     zero) - the net/variable realizations have no storage. The
	 *     endpoints map also grows to cover both signal indices.
	 *   Case 2: IDEMPOTENCE. A SECOND call of
	 *     pigen_lower_rtl_module_declarations on the SAME lowering and
	 *     module publishes NOTHING new: object_count, expression_count and
	 *     instance_count are all unchanged, and each signal's endpoints
	 *     record is byte-identical to after the first call.
	 * The payload object kind (section (9)), the constant values and literal
	 * widths (section (10)) and the per-control record contents are NOT
	 * re-asserted here; only the arena count deltas, the no-instance pin and
	 * the second-pass idempotence are pinned in this section.
	 * Green: every owner-construction and descriptor assert passes
	 * against the landed owners (including the already-implemented
	 * pigen_lower_rtl_type memo, which resolves the 8-bit type); the
	 * declaration lowering (Case 1's `rc == 0`) and every later
	 * assert in this section pass against the landed lowering.
	 * No stale red marker remains in this
	 * file. */
	SECTION("t6-net-variable-counts") {
		const char *text =
			"module netvar : wire a ; reg b ;\n";
		pigen_source_manager sources_g = {0};
		pigen_semantic_model sem_g;
		pigen_rtl_model rtl_g = {0};
		pigen_rtl_lowering lowering_g;
		pigen_data_type_id unsized;
		pigen_data_type_id t8;
		pigen_const_expr_id width8;
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name_a;
		pigen_source_span name_b;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id wire_symbol;
		pigen_symbol_id reg_symbol;
		pigen_module_id module;
		pigen_signal_id wire;
		pigen_signal_id reg;
		const pigen_semantic_signal *wire_owner;
		const pigen_semantic_signal *reg_owner;
		const pigen_transfer_type_descriptor *wire_descriptor;
		const pigen_transfer_type_descriptor *reg_descriptor;
		pigen_rtl_type_id lowered_t8;
		const pigen_rtl_type *t8_record;
		pigen_rtl_signal_endpoints wire_record;
		pigen_rtl_signal_endpoints reg_record;
		pigen_rtl_signal_endpoints wire_again;
		pigen_rtl_signal_endpoints reg_again;
		size_t object_before;
		size_t expression_before;
		size_t instance_before;
		size_t object_delta;
		size_t expression_delta;
		size_t instance_delta;
		size_t endpoint_map_before;
		int rc;
		int no_storage;
		int idempotent;

		/* Build the net/variable module through the owner APIs (section
		 * (8)'s landed construction pattern, two INTERNAL signals of the
		 * same 8-bit type): the type first, then a source file, the
		 * compilation scope, a module in it, and the two declared signals.
		 * The module symbol's declaration spans the whole file (as
		 * module_add requires), the module scope uses the same span, and
		 * each signal symbol's name span is contained in and its
		 * declaration span equal to the span signal_add checks. */
		pigen_semantic_init(&sem_g, &sources_g);
		pigen_rtl_lowering_init(&lowering_g, &sem_g, &rtl_g);
		unsized = pigen_data_type_unsized_integer(&sem_g);
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem_g, 8, unsized);
		t8 = pigen_data_type_unsigned_integer(&sem_g, width8);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(t8));
		source = pigen_source_add(&sources_g, "lower_netvar_counts.pigen",
			text, strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name_a = (pigen_source_span){source, 21, 22}; /* "a" */
		name_b = (pigen_source_span){source, 29, 30}; /* "b" */
		sem_g.compilation_scope = pigen_scope_add(&sem_g,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem_g.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_g, sem_g.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem_g, sem_g.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem_g, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_g, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_a, name_a, &wire_symbol, NULL) == PIGEN_DECLARE_OK);
		wire = pigen_signal_add(&sem_g, (pigen_syntax_id){2}, module,
			wire_symbol, t8, pigen_semantic_scalar_shape(&sem_g),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_WIRE,
			PIGEN_SEMANTIC_INTERNAL, name_a);
		REQUIRE(wire.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_g, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_b, name_b, &reg_symbol, NULL) == PIGEN_DECLARE_OK);
		reg = pigen_signal_add(&sem_g, (pigen_syntax_id){3}, module,
			reg_symbol, t8, pigen_semantic_scalar_shape(&sem_g),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_REG,
			PIGEN_SEMANTIC_INTERNAL, name_b);
		REQUIRE(reg.index != PIGEN_INVALID_ID);

		/* Owner facts the contract rides on (all green against the landed
		 * owners): both signal records report their transfer type, the
		 * INTERNAL direction and the module; WIRE and REG are concrete
		 * descriptors mapped to the COMBINATIONAL_NET and
		 * PROCEDURAL_VARIABLE realizations; and the already-implemented
		 * pigen_lower_rtl_type memo resolves the 8-bit data type to a
		 * record in the RTL model. The type memo is the only lowering work
		 * done before the declaration call, so the count snapshots below
		 * are taken after it. */
		wire_owner = pigen_signal_get(&sem_g, wire);
		REQUIRE(wire_owner &&
			wire_owner->transfer_type == PIGEN_TRANSFER_TYPE_WIRE &&
			wire_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			wire_owner->module.index == module.index);
		reg_owner = pigen_signal_get(&sem_g, reg);
		REQUIRE(reg_owner &&
			reg_owner->transfer_type == PIGEN_TRANSFER_TYPE_REG &&
			reg_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			reg_owner->module.index == module.index);
		wire_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_WIRE);
		REQUIRE(wire_descriptor && wire_descriptor->is_concrete &&
			wire_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET);
		reg_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_REG);
		REQUIRE(reg_descriptor && reg_descriptor->is_concrete &&
			reg_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE);
		lowered_t8 = pigen_lower_rtl_type(&lowering_g, t8);
		REQUIRE(!IS_INVALID_ID(lowered_t8));
		t8_record = pigen_rtl_type_get(&rtl_g, lowered_t8);
		REQUIRE(t8_record);

		/* Case 1: COUNT DELTAS. Snapshot all three arenas BEFORE the first
		 * declaration call; the first `rc == 0` is green against the
		 * landed lowering, which reports success.
		 * The net/variable realizations have no storage: the object
		 * delta is exactly one payload per signal, the expression delta
		 * is bounded by the two constant controls per signal, and no
		 * pigen_rtl_instance is published at all. */
		object_before = rtl_g.object_count;
		expression_before = rtl_g.expression_count;
		instance_before = rtl_g.instance_count;
		REQUIRE(instance_before == 0); /* no instances before the call */
		rc = pigen_lower_rtl_module_declarations(&lowering_g, module);
		REQUIRE(rc == 0); /* Green against the landed lowering: the
		 * declaration lowering reports success. */
		REQUIRE(lowering_g.lowered_endpoint_count >
			(wire.index > reg.index ? wire.index : reg.index));
		object_delta = rtl_g.object_count - object_before;
		expression_delta = rtl_g.expression_count - expression_before;
		instance_delta = rtl_g.instance_count - instance_before;
		/* Exactly one payload object per signal: the two constant controls
		 * (valid/ready) publish no objects, so the object delta is exactly
		 * two for the whole module. An implementation that also publishes
		 * an object for a constant control - or shares one payload object
		 * between the two signals - is caught here. */
		no_storage = object_delta == 2 && instance_delta == 0 &&
			expression_delta >= 2 && expression_delta <= 4;
		REQUIRE(no_storage); /* Green: the landed lowering publishes one payload, no instance. */

		/* Case 2: IDEMPOTENCE. A second call on the SAME lowering and
		 * module publishes NOTHING new: all three arena counts are
		 * unchanged and each signal's endpoints record is byte-identical
		 * to after the first call. Green against the landed lowering,
		 * which republishes nothing on the second pass. */
		wire_record = lowering_g.lowered_endpoints[wire.index];
		reg_record = lowering_g.lowered_endpoints[reg.index];
		object_before = rtl_g.object_count;
		expression_before = rtl_g.expression_count;
		instance_before = rtl_g.instance_count;
		endpoint_map_before = lowering_g.lowered_endpoint_count;
		rc = pigen_lower_rtl_module_declarations(&lowering_g, module);
		REQUIRE(rc == 0); /* Green against the landed lowering. */
		wire_again = lowering_g.lowered_endpoints[wire.index];
		reg_again = lowering_g.lowered_endpoints[reg.index];
		idempotent = rtl_g.object_count == object_before &&
			rtl_g.expression_count == expression_before &&
			rtl_g.instance_count == instance_before &&
			lowering_g.lowered_endpoint_count == endpoint_map_before &&
			(wire_again.payload.index == wire_record.payload.index) &&
			(wire_again.valid.index == wire_record.valid.index) &&
			(wire_again.ready.index == wire_record.ready.index) &&
			(wire_again.input_payload.index ==
				wire_record.input_payload.index) &&
			(wire_again.input_valid.index == wire_record.input_valid.index) &&
			(wire_again.input_ready.index ==
				wire_record.input_ready.index) &&
			(reg_again.payload.index == reg_record.payload.index) &&
			(reg_again.valid.index == reg_record.valid.index) &&
			(reg_again.ready.index == reg_record.ready.index) &&
			(reg_again.input_payload.index ==
				reg_record.input_payload.index) &&
			(reg_again.input_valid.index == reg_record.input_valid.index) &&
			(reg_again.input_ready.index == reg_record.input_ready.index);
		REQUIRE(idempotent); /* Green: the landed lowering is idempotent. */

		pigen_rtl_lowering_free(&lowering_g);
		pigen_free_rtl_model(&rtl_g);
		pigen_free_semantic_model(&sem_g);
		pigen_free_sources(&sources_g);
	}

	/* (12) Storage realization matrix: the ELASTIC_SLOT / PULSE_REGISTER /
	 * PARAMETERIZED_QUEUE / SKID_QUEUE construction + declaration-success +
	 * per-signal payload contract for pigen_lower_rtl_module_declarations
	 * (Task 6, "elastic slot / pulse / queue / skid -> corresponding
	 * primitive structure"). One module holds FIVE signals of the SAME
	 * 8-bit unsigned data type: a PIGEN_TRANSFER_TYPE_BUF (ELASTIC_SLOT),
	 * a PIGEN_TRANSFER_TYPE_PORT (PULSE_REGISTER), a
	 * PIGEN_TRANSFER_TYPE_FIFO (PARAMETERIZED_QUEUE) whose
	 * transfer_argument is a constant 4 expression of the same type (the
	 * owner's PIGEN_TRANSFER_PARAMETER_DEPTH requirement: a valid
	 * constant-expression identity), a PIGEN_TRANSFER_TYPE_SKID
	 * (SKID_QUEUE) - the four built PIGEN_SEMANTIC_INTERNAL - and a FIFTH
	 * PIGEN_TRANSFER_TYPE_BUF (ELASTIC_SLOT) built PIGEN_SEMANTIC_OUTPUT:
	 * the owner admits a concrete storage signal with a non-INTERNAL
	 * direction (for a concrete descriptor the only direction constraint
	 * is INTERNAL..INOUT). The three non-FIFO INTERNAL signals and the
	 * OUTPUT signal pass the exact invalid transfer-argument form, because
	 * the owner rejects any non-invalid argument for
	 * PIGEN_TRANSFER_PARAMETER_NONE descriptors. Built through the owner
	 * APIs exactly as section (8)'s landed construction pattern: a source
	 * file, the compilation scope, a module symbol + pigen_module_add, a
	 * PIGEN_SYMBOL_SIGNAL declaration per signal (matching data type and
	 * declaration span) and pigen_signal_add with the signal's direction.
	 *   Case 1: STORAGE MATRIX. The declaration lowering reports success
	 *     and the endpoints map grows past ALL FIVE signal indices. For
	 *     EACH signal, the endpoints payload is a valid id resolving to a
	 *     pigen_rtl_object of kind PIGEN_RTL_OBJECT_VARIABLE whose type is
	 *     the lowered 8-bit type - the SAME record the landed
	 *     pigen_lower_rtl_type memo yields - and whose direction equals
	 *     THAT signal's own semantic direction (the four INTERNAL signals
	 *     pin PIGEN_SEMANTIC_INTERNAL, the OUTPUT signal pins
	 *     PIGEN_SEMANTIC_OUTPUT - never one hardcoded module direction):
	 *     a direction-ignoring lowering that stamps every payload
	 *     PIGEN_SEMANTIC_INTERNAL is caught by the OUTPUT signal. For
	 *     EACH signal the input side is pinned as a direct per-signal
	 *     object identity: input_payload.index == payload.index, and
	 *     input_valid.index and input_ready.index equal that same object
	 *     identity - the storage input side exposes the one payload
	 *     object. An input-side-dropping lowering (all input_* fields
	 *     PIGEN_INVALID_ID) is caught here even though the section (15)
	 *     idempotence loop compares two records of the same lowering and
	 *     passes equal-invalid records.
	 * Exclusions (sibling sections, NOT pinned here): the per-signal
	 * instance publication, the four distinct instance shapes and the
	 * cross-contamination pin (the storage-instance-shapes section); the
	 * FIFO depth round-trip into the instance parameters range and the
	 * descriptor facts (the storage-fifo-depth section); every arena
	 * count delta and the second-pass idempotence (the
	 * storage-count-deltas section).
	 * Status: green - every assert passes against the landed storage
	 * lowering, which carries the signal's semantic direction into the
	 * payload object and exposes the same payload object on the input
	 * side. This section is pure coverage tightening of two spec-
	 * explicit obligations the four-signal matrix did not yet pin. */
	SECTION("t6-storage-matrix") {
		const char *text =
			"module store : buf a ; port b ; fifo c ; skid d ; buf e ;\n";
		pigen_source_manager sources_h = {0};
		pigen_semantic_model sem_h;
		pigen_rtl_model rtl_h = {0};
		pigen_rtl_lowering lowering_h;
		pigen_data_type_id unsized;
		pigen_data_type_id t8;
		pigen_const_expr_id width8;
		pigen_expr_id depth4;
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name_a;
		pigen_source_span name_b;
		pigen_source_span name_c;
		pigen_source_span name_d;
		pigen_source_span name_e;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id buf_symbol;
		pigen_symbol_id port_symbol;
		pigen_symbol_id fifo_symbol;
		pigen_symbol_id skid_symbol;
		pigen_symbol_id out_symbol;
		pigen_module_id module;
		pigen_signal_id buf;
		pigen_signal_id port;
		pigen_signal_id fifo;
		pigen_signal_id skid;
		pigen_signal_id out;
		const pigen_semantic_signal *buf_owner;
		const pigen_semantic_signal *port_owner;
		const pigen_semantic_signal *fifo_owner;
		const pigen_semantic_signal *skid_owner;
		const pigen_semantic_signal *out_owner;
		const pigen_transfer_type_descriptor *buf_descriptor;
		const pigen_transfer_type_descriptor *port_descriptor;
		const pigen_transfer_type_descriptor *fifo_descriptor;
		const pigen_transfer_type_descriptor *skid_descriptor;
		pigen_rtl_type_id lowered_t8;
		const pigen_rtl_type *t8_record;
		const pigen_rtl_signal_endpoints *buf_endpoints;
		const pigen_rtl_signal_endpoints *port_endpoints;
		const pigen_rtl_signal_endpoints *fifo_endpoints;
		const pigen_rtl_signal_endpoints *skid_endpoints;
		const pigen_rtl_signal_endpoints *out_endpoints;
		const pigen_rtl_object *buf_payload_obj;
		const pigen_rtl_object *port_payload_obj;
		const pigen_rtl_object *fifo_payload_obj;
		const pigen_rtl_object *skid_payload_obj;
		const pigen_rtl_object *out_payload_obj;
		const pigen_rtl_type *payload_type;
		int rc;
		int payload_ok;
		int input_ok;

		/* Build the storage module through the owner APIs (section (8)'s
		 * landed construction pattern, four INTERNAL signals of the same
		 * 8-bit type): the type first, then the depth argument over that
		 * type, a source file, the compilation scope, a module in it, and
		 * the four declared signals. The module symbol's declaration
		 * spans the whole file (as module_add requires), the module scope
		 * uses the same span, and each signal symbol's name span is
		 * contained in and its declaration span equal to the span
		 * signal_add checks. */
		pigen_semantic_init(&sem_h, &sources_h);
		pigen_rtl_lowering_init(&lowering_h, &sem_h, &rtl_h);
		unsized = pigen_data_type_unsized_integer(&sem_h);
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem_h, 8, unsized);
		t8 = pigen_data_type_unsigned_integer(&sem_h, width8);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(t8));
		source = pigen_source_add(&sources_h, "lower_storage_matrix.pigen",
			text, strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name_a = (pigen_source_span){source, 19, 20}; /* "a" */
		name_b = (pigen_source_span){source, 28, 29}; /* "b" */
		name_c = (pigen_source_span){source, 37, 38}; /* "c" */
		name_d = (pigen_source_span){source, 46, 47}; /* "d" */
		name_e = (pigen_source_span){source, 54, 55}; /* "e" */
		/* The FIFO depth: a real expression over the interned constant 4
		 * of the 8-bit type - the valid constant-expression identity the
		 * owner requires for PIGEN_TRANSFER_PARAMETER_DEPTH descriptors. */
		depth4 = pigen_expr_add_integer(&sem_h, 4, t8, name_c);
		REQUIRE(!IS_INVALID_ID(depth4));
		sem_h.compilation_scope = pigen_scope_add(&sem_h,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem_h.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_h, sem_h.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem_h, sem_h.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem_h, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_h, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_a, name_a, &buf_symbol, NULL) == PIGEN_DECLARE_OK);
		buf = pigen_signal_add(&sem_h, (pigen_syntax_id){2}, module,
			buf_symbol, t8, pigen_semantic_scalar_shape(&sem_h),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
			PIGEN_SEMANTIC_INTERNAL, name_a);
		REQUIRE(buf.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_h, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_b, name_b, &port_symbol, NULL) == PIGEN_DECLARE_OK);
		port = pigen_signal_add(&sem_h, (pigen_syntax_id){3}, module,
			port_symbol, t8, pigen_semantic_scalar_shape(&sem_h),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_PORT,
			PIGEN_SEMANTIC_INTERNAL, name_b);
		REQUIRE(port.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_h, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_c, name_c, &fifo_symbol, NULL) == PIGEN_DECLARE_OK);
		fifo = pigen_signal_add(&sem_h, (pigen_syntax_id){4}, module,
			fifo_symbol, t8, pigen_semantic_scalar_shape(&sem_h),
			depth4, PIGEN_TRANSFER_TYPE_FIFO,
			PIGEN_SEMANTIC_INTERNAL, name_c);
		REQUIRE(fifo.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_h, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_d, name_d, &skid_symbol, NULL) == PIGEN_DECLARE_OK);
		skid = pigen_signal_add(&sem_h, (pigen_syntax_id){5}, module,
			skid_symbol, t8, pigen_semantic_scalar_shape(&sem_h),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_SKID,
			PIGEN_SEMANTIC_INTERNAL, name_d);
		REQUIRE(skid.index != PIGEN_INVALID_ID);
		/* The FIFTH signal: a PIGEN_TRANSFER_TYPE_BUF (ELASTIC_SLOT) with a
		 * NON-INTERNAL direction - the owner admits PIGEN_SEMANTIC_OUTPUT
		 * for concrete descriptors (the only direction constraint is
		 * INTERNAL..INOUT) - so a direction-ignoring storage lowering that
		 * stamps every payload PIGEN_SEMANTIC_INTERNAL is caught by this
		 * signal's direction pin below. */
		REQUIRE(pigen_symbol_declare(&sem_h, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_e, name_e, &out_symbol, NULL) == PIGEN_DECLARE_OK);
		out = pigen_signal_add(&sem_h, (pigen_syntax_id){6}, module,
			out_symbol, t8, pigen_semantic_scalar_shape(&sem_h),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
			PIGEN_SEMANTIC_OUTPUT, name_e);
		REQUIRE(out.index != PIGEN_INVALID_ID);

		/* Owner facts the contract rides on (all green against the landed
		 * owners): all five signal records report their transfer type,
		 * their direction (the first four INTERNAL, the fifth OUTPUT) and
		 * the module; BUF, PORT, FIFO and SKID are concrete descriptors
		 * mapped to the ELASTIC_SLOT, PULSE_REGISTER, PARAMETERIZED_QUEUE
		 * and SKID_QUEUE realizations, respectively; only FIFO carries a
		 * transfer parameter (PIGEN_TRANSFER_PARAMETER_DEPTH) and its
		 * transfer_argument is the constant 4 expression over the 8-bit
		 * type - a valid constant-expression identity the owner
		 * requires; the other four carry the invalid argument; and the
		 * already-implemented pigen_lower_rtl_type memo resolves the
		 * 8-bit data type to a record in the RTL model. */
		buf_owner = pigen_signal_get(&sem_h, buf);
		REQUIRE(buf_owner &&
			buf_owner->transfer_type == PIGEN_TRANSFER_TYPE_BUF &&
			buf_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			buf_owner->module.index == module.index &&
			IS_INVALID_ID(buf_owner->transfer_argument));
		port_owner = pigen_signal_get(&sem_h, port);
		REQUIRE(port_owner &&
			port_owner->transfer_type == PIGEN_TRANSFER_TYPE_PORT &&
			port_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			port_owner->module.index == module.index &&
			IS_INVALID_ID(port_owner->transfer_argument));
		fifo_owner = pigen_signal_get(&sem_h, fifo);
		REQUIRE(fifo_owner &&
			fifo_owner->transfer_type == PIGEN_TRANSFER_TYPE_FIFO &&
			fifo_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			fifo_owner->module.index == module.index &&
			fifo_owner->transfer_argument.index == depth4.index);
		skid_owner = pigen_signal_get(&sem_h, skid);
		REQUIRE(skid_owner &&
			skid_owner->transfer_type == PIGEN_TRANSFER_TYPE_SKID &&
			skid_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			skid_owner->module.index == module.index &&
			IS_INVALID_ID(skid_owner->transfer_argument));
		/* The OUTPUT-direction signal is owner-constructible and reports
		 * exactly that direction: the witness for the direction pin in
		 * Case 1. Its descriptor is the same BUF descriptor pinned for
		 * the first signal (ELASTIC_SLOT, no transfer parameter). */
		out_owner = pigen_signal_get(&sem_h, out);
		REQUIRE(out_owner &&
			out_owner->transfer_type == PIGEN_TRANSFER_TYPE_BUF &&
			out_owner->direction == PIGEN_SEMANTIC_OUTPUT &&
			out_owner->module.index == module.index &&
			IS_INVALID_ID(out_owner->transfer_argument));
		buf_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_BUF);
		REQUIRE(buf_descriptor && buf_descriptor->is_concrete &&
			buf_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE &&
			buf_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT);
		port_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_PORT);
		REQUIRE(port_descriptor && port_descriptor->is_concrete &&
			port_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE &&
			port_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER);
		fifo_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_FIFO);
		REQUIRE(fifo_descriptor && fifo_descriptor->is_concrete &&
			fifo_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_DEPTH &&
			fifo_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE);
		skid_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_SKID);
		REQUIRE(skid_descriptor && skid_descriptor->is_concrete &&
			skid_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE &&
			skid_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_SKID_QUEUE);
		REQUIRE(pigen_expr_get(&sem_h, depth4) &&
			pigen_expr_constant(&sem_h, depth4).index != PIGEN_INVALID_ID);
		lowered_t8 = pigen_lower_rtl_type(&lowering_h, t8);
		REQUIRE(!IS_INVALID_ID(lowered_t8));
		t8_record = pigen_rtl_type_get(&rtl_h, lowered_t8);
		REQUIRE(t8_record);

		/* Case 1: STORAGE MATRIX. The declaration lowering reports
		 * success and the endpoints map covers ALL FIVE signals. */
		rc = pigen_lower_rtl_module_declarations(&lowering_h, module);
		REQUIRE(rc == 0);
		{
			uint32_t max_index = buf.index > port.index ?
				buf.index : port.index;
			max_index = max_index > fifo.index ?
				max_index : fifo.index;
			max_index = max_index > skid.index ?
				max_index : skid.index;
			max_index = max_index > out.index ?
				max_index : out.index;
			REQUIRE(lowering_h.lowered_endpoint_count > max_index);
		}

		/* For EACH signal the payload is a resolvable VARIABLE object:
		 * kind PIGEN_RTL_OBJECT_VARIABLE, type the lowered 8-bit type -
		 * the SAME record the pigen_lower_rtl_type memo yielded - and
		 * direction THAT signal's own semantic direction (pinned
		 * per-signal against the owner record, never one hardcoded
		 * module direction): the four INTERNAL signals pin
		 * PIGEN_SEMANTIC_INTERNAL, the OUTPUT signal pins
		 * PIGEN_SEMANTIC_OUTPUT. An implementation that lowers a
		 * storage signal to a non-VARIABLE object, gives it a wrong or
		 * missing type, ignores the signal's direction, or skips a
		 * signal is caught here. */
		buf_endpoints = &lowering_h.lowered_endpoints[buf.index];
		port_endpoints = &lowering_h.lowered_endpoints[port.index];
		fifo_endpoints = &lowering_h.lowered_endpoints[fifo.index];
		skid_endpoints = &lowering_h.lowered_endpoints[skid.index];
		out_endpoints = &lowering_h.lowered_endpoints[out.index];
		payload_ok =
			(!IS_INVALID_ID(buf_endpoints->payload)) &&
			(!IS_INVALID_ID(port_endpoints->payload)) &&
			(!IS_INVALID_ID(fifo_endpoints->payload)) &&
			(!IS_INVALID_ID(skid_endpoints->payload)) &&
			(!IS_INVALID_ID(out_endpoints->payload));
		buf_payload_obj = payload_ok ? pigen_rtl_object_get(&rtl_h,
			buf_endpoints->payload) : NULL;
		port_payload_obj = payload_ok ? pigen_rtl_object_get(&rtl_h,
			port_endpoints->payload) : NULL;
		fifo_payload_obj = payload_ok ? pigen_rtl_object_get(&rtl_h,
			fifo_endpoints->payload) : NULL;
		skid_payload_obj = payload_ok ? pigen_rtl_object_get(&rtl_h,
			skid_endpoints->payload) : NULL;
		out_payload_obj = payload_ok ? pigen_rtl_object_get(&rtl_h,
			out_endpoints->payload) : NULL;
		payload_type = pigen_rtl_type_get(&rtl_h, buf_payload_obj ?
			buf_payload_obj->type : (pigen_rtl_type_id){PIGEN_INVALID_ID});
		payload_ok = payload_ok && buf_payload_obj &&
			buf_payload_obj->kind == PIGEN_RTL_OBJECT_VARIABLE &&
			payload_type == t8_record &&
			buf_payload_obj->direction == buf_owner->direction;
		payload_type = pigen_rtl_type_get(&rtl_h, port_payload_obj ?
			port_payload_obj->type : (pigen_rtl_type_id){PIGEN_INVALID_ID});
		payload_ok = payload_ok && port_payload_obj &&
			port_payload_obj->kind == PIGEN_RTL_OBJECT_VARIABLE &&
			payload_type == t8_record &&
			port_payload_obj->direction == port_owner->direction;
		payload_type = pigen_rtl_type_get(&rtl_h, fifo_payload_obj ?
			fifo_payload_obj->type : (pigen_rtl_type_id){PIGEN_INVALID_ID});
		payload_ok = payload_ok && fifo_payload_obj &&
			fifo_payload_obj->kind == PIGEN_RTL_OBJECT_VARIABLE &&
			payload_type == t8_record &&
			fifo_payload_obj->direction == fifo_owner->direction;
		payload_type = pigen_rtl_type_get(&rtl_h, skid_payload_obj ?
			skid_payload_obj->type : (pigen_rtl_type_id){PIGEN_INVALID_ID});
		payload_ok = payload_ok && skid_payload_obj &&
			skid_payload_obj->kind == PIGEN_RTL_OBJECT_VARIABLE &&
			payload_type == t8_record &&
			skid_payload_obj->direction == skid_owner->direction;
		payload_type = pigen_rtl_type_get(&rtl_h, out_payload_obj ?
			out_payload_obj->type : (pigen_rtl_type_id){PIGEN_INVALID_ID});
		payload_ok = payload_ok && out_payload_obj &&
			out_payload_obj->kind == PIGEN_RTL_OBJECT_VARIABLE &&
			payload_type == t8_record &&
			out_payload_obj->direction == out_owner->direction;
		REQUIRE(payload_ok);

		/* For EACH signal the INPUT SIDE exposes the same payload object:
		 * input_payload.index == payload.index, and input_valid.index
		 * and input_ready.index equal that same object identity (the
		 * storage input side is the one payload object itself). This is
		 * a direct per-signal identity pin: the section (15)
		 * idempotence loop compares two records of the same lowering,
		 * so an input-side-dropping lowering that leaves all three
		 * input_* fields PIGEN_INVALID_ID on every call compares
		 * equal-invalid records and would pass that loop; this pin
		 * catches it. */
		input_ok =
			(buf_endpoints->input_payload.index ==
				buf_endpoints->payload.index) &&
			(buf_endpoints->input_valid.index ==
				buf_endpoints->payload.index) &&
			(buf_endpoints->input_ready.index ==
				buf_endpoints->payload.index) &&
			(port_endpoints->input_payload.index ==
				port_endpoints->payload.index) &&
			(port_endpoints->input_valid.index ==
				port_endpoints->payload.index) &&
			(port_endpoints->input_ready.index ==
				port_endpoints->payload.index) &&
			(fifo_endpoints->input_payload.index ==
				fifo_endpoints->payload.index) &&
			(fifo_endpoints->input_valid.index ==
				fifo_endpoints->payload.index) &&
			(fifo_endpoints->input_ready.index ==
				fifo_endpoints->payload.index) &&
			(skid_endpoints->input_payload.index ==
				skid_endpoints->payload.index) &&
			(skid_endpoints->input_valid.index ==
				skid_endpoints->payload.index) &&
			(skid_endpoints->input_ready.index ==
				skid_endpoints->payload.index) &&
			(out_endpoints->input_payload.index ==
				out_endpoints->payload.index) &&
			(out_endpoints->input_valid.index ==
				out_endpoints->payload.index) &&
			(out_endpoints->input_ready.index ==
				out_endpoints->payload.index);
		REQUIRE(input_ok);

		pigen_rtl_lowering_free(&lowering_h);
		pigen_free_rtl_model(&rtl_h);
		pigen_free_semantic_model(&sem_h);
		pigen_free_sources(&sources_h);
	}

	/* (13) Storage realization instances: the DISTINCT-INSTANCE publication
	 * and cross-contamination contract for
	 * pigen_lower_rtl_module_declarations (Task 6, "elastic slot / pulse /
	 * queue / skid -> corresponding primitive structure"). The SAME
	 * four-signal storage module sibling section (12) builds: one 8-bit
	 * unsigned data type, FOUR PIGEN_SEMANTIC_INTERNAL signals in one
	 * module - a PIGEN_TRANSFER_TYPE_BUF (ELASTIC_SLOT), a
	 * PIGEN_TRANSFER_TYPE_PORT (PULSE_REGISTER), a PIGEN_TRANSFER_TYPE_FIFO
	 * (PARAMETERIZED_QUEUE) whose transfer_argument is the constant 4
	 * expression of the same 8-bit type (the owner's
	 * PIGEN_TRANSFER_PARAMETER_DEPTH requirement: a valid
	 * constant-expression identity), and a PIGEN_TRANSFER_TYPE_SKID
	 * (SKID_QUEUE) - the three non-FIFO signals passing the exact invalid
	 * (pigen_expr_id){PIGEN_INVALID_ID} transfer-argument form the owner
	 * requires for PIGEN_TRANSFER_PARAMETER_NONE descriptors. Built
	 * through the owner APIs exactly as section (12) does. Checked
	 * through pigen_rtl_lowering_init +
	 * pigen_lower_rtl_module_declarations + the RTL model's instance arena
	 * (pigen_rtl_model.instances / .instance_count; the record is
	 * pigen_rtl_instance in include/pigen/rtl.h:111-117):
	 *   Case 1: DISTINCT INSTANCES. The call reports success (rc == 0),
	 *     green against the landed lowering (not a compile or harness
	 *     error); the instance arena
	 *     grows by exactly four, as the landed lowering publishes one
	 *     instance per signal (snapshotted before the call); and the
	 *     four newly published records are MUTUALLY DISTINCT - no two of
	 *     the four records are field-identical (origin, semantic_module,
	 *     parameters, connections and module compared), so the set of
	 *     newly published instance ids has cardinality exactly four and
	 *     no instance is published twice. An implementation that
	 *     publishes one shared instance for all four signals, publishes
	 *     the same instance for two signals, or publishes any other
	 *     number of instances is caught here.
	 * Exclusions (sibling sections, NOT pinned here): the payload object
	 * kind/type/direction and endpoints past all four indices (sibling
	 * section (12) storage-matrix-success); the FIFO depth value, the
	 * parameters-record round-trip and the descriptor facts (sibling
	 * section storage-fifo-depth); every arena count delta and the
	 * second-pass idempotence (sibling section storage-count-deltas);
	 * and which primitive each instance instantiates
	 * (pigen_buf/pigen_port/pigen_fifo/pigen_skid) - there is no owner
	 * API resolving a pigen_rtl_module_id to a primitive definition name
	 * and the instance's module field is invalid until a later
	 * owner-resolution stage, so that mapping is an implementation design
	 * decision, not an owner-checkable fact here.
	 * Green: every owner-construction and descriptor assert passes
	 * against the landed owners; the declaration lowering (Case 1's
	 * `rc == 0`) and every later assert in this section pass against
	 * the landed lowering, which reports success. No stale red marker
	 * remains in this file. */
	SECTION("t6-storage-instances") {
		const char *text =
			"module store : buf a ; port b ; fifo c ; skid d ;\n";
		pigen_source_manager sources_i = {0};
		pigen_semantic_model sem_i;
		pigen_rtl_model rtl_i = {0};
		pigen_rtl_lowering lowering_i;
		pigen_data_type_id unsized;
		pigen_data_type_id t8;
		pigen_const_expr_id width8;
		pigen_expr_id depth4;
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name_a;
		pigen_source_span name_b;
		pigen_source_span name_c;
		pigen_source_span name_d;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id buf_symbol;
		pigen_symbol_id port_symbol;
		pigen_symbol_id fifo_symbol;
		pigen_symbol_id skid_symbol;
		pigen_module_id module;
		pigen_signal_id buf;
		pigen_signal_id port;
		pigen_signal_id fifo;
		pigen_signal_id skid;
		const pigen_semantic_signal *buf_owner;
		const pigen_semantic_signal *port_owner;
		const pigen_semantic_signal *fifo_owner;
		const pigen_semantic_signal *skid_owner;
		const pigen_transfer_type_descriptor *buf_descriptor;
		const pigen_transfer_type_descriptor *port_descriptor;
		const pigen_transfer_type_descriptor *fifo_descriptor;
		const pigen_transfer_type_descriptor *skid_descriptor;
		size_t instance_before;
		int rc;
		int exact_delta;
		int distinct;

		/* Build the storage module through the owner APIs (section (12)'s
		 * landed construction pattern, four INTERNAL signals of the same
		 * 8-bit type): the type first, then the depth argument over that
		 * type, a source file, the compilation scope, a module in it, and
		 * the four declared signals. The module symbol's declaration
		 * spans the whole file (as module_add requires), the module scope
		 * uses the same span, and each signal symbol's name span is
		 * contained in and its declaration span equal to the span
		 * signal_add checks. */
		pigen_semantic_init(&sem_i, &sources_i);
		pigen_rtl_lowering_init(&lowering_i, &sem_i, &rtl_i);
		unsized = pigen_data_type_unsized_integer(&sem_i);
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem_i, 8, unsized);
		t8 = pigen_data_type_unsigned_integer(&sem_i, width8);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(t8));
		source = pigen_source_add(&sources_i,
			"lower_storage_instances.pigen", text, strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name_a = (pigen_source_span){source, 19, 20}; /* "a" */
		name_b = (pigen_source_span){source, 28, 29}; /* "b" */
		name_c = (pigen_source_span){source, 37, 38}; /* "c" */
		name_d = (pigen_source_span){source, 46, 47}; /* "d" */
		/* The FIFO depth: a real expression over the interned constant 4
		 * of the 8-bit type - the valid constant-expression identity the
		 * owner requires for PIGEN_TRANSFER_PARAMETER_DEPTH descriptors. */
		depth4 = pigen_expr_add_integer(&sem_i, 4, t8, name_c);
		REQUIRE(!IS_INVALID_ID(depth4));
		sem_i.compilation_scope = pigen_scope_add(&sem_i,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem_i.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_i, sem_i.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem_i, sem_i.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem_i, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_i, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_a, name_a, &buf_symbol, NULL) == PIGEN_DECLARE_OK);
		buf = pigen_signal_add(&sem_i, (pigen_syntax_id){2}, module,
			buf_symbol, t8, pigen_semantic_scalar_shape(&sem_i),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
			PIGEN_SEMANTIC_INTERNAL, name_a);
		REQUIRE(buf.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_i, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_b, name_b, &port_symbol, NULL) == PIGEN_DECLARE_OK);
		port = pigen_signal_add(&sem_i, (pigen_syntax_id){3}, module,
			port_symbol, t8, pigen_semantic_scalar_shape(&sem_i),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_PORT,
			PIGEN_SEMANTIC_INTERNAL, name_b);
		REQUIRE(port.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_i, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_c, name_c, &fifo_symbol, NULL) == PIGEN_DECLARE_OK);
		fifo = pigen_signal_add(&sem_i, (pigen_syntax_id){4}, module,
			fifo_symbol, t8, pigen_semantic_scalar_shape(&sem_i),
			depth4, PIGEN_TRANSFER_TYPE_FIFO,
			PIGEN_SEMANTIC_INTERNAL, name_c);
		REQUIRE(fifo.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_i, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_d, name_d, &skid_symbol, NULL) == PIGEN_DECLARE_OK);
		skid = pigen_signal_add(&sem_i, (pigen_syntax_id){5}, module,
			skid_symbol, t8, pigen_semantic_scalar_shape(&sem_i),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_SKID,
			PIGEN_SEMANTIC_INTERNAL, name_d);
		REQUIRE(skid.index != PIGEN_INVALID_ID);

		/* Owner facts the contract rides on (all green against the landed
		 * owners): all four signal records report their transfer type,
		 * the INTERNAL direction and the module; BUF, PORT, FIFO and
		 * SKID are concrete descriptors mapped to the ELASTIC_SLOT,
		 * PULSE_REGISTER, PARAMETERIZED_QUEUE and SKID_QUEUE
		 * realizations, respectively; only FIFO carries a transfer
		 * parameter (PIGEN_TRANSFER_PARAMETER_DEPTH) and its
		 * transfer_argument is the constant 4 expression over the 8-bit
		 * type - a valid constant-expression identity the owner
		 * requires; the other three carry the invalid argument. */
		buf_owner = pigen_signal_get(&sem_i, buf);
		REQUIRE(buf_owner &&
			buf_owner->transfer_type == PIGEN_TRANSFER_TYPE_BUF &&
			buf_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			buf_owner->module.index == module.index &&
			IS_INVALID_ID(buf_owner->transfer_argument));
		port_owner = pigen_signal_get(&sem_i, port);
		REQUIRE(port_owner &&
			port_owner->transfer_type == PIGEN_TRANSFER_TYPE_PORT &&
			port_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			port_owner->module.index == module.index &&
			IS_INVALID_ID(port_owner->transfer_argument));
		fifo_owner = pigen_signal_get(&sem_i, fifo);
		REQUIRE(fifo_owner &&
			fifo_owner->transfer_type == PIGEN_TRANSFER_TYPE_FIFO &&
			fifo_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			fifo_owner->module.index == module.index &&
			fifo_owner->transfer_argument.index == depth4.index);
		skid_owner = pigen_signal_get(&sem_i, skid);
		REQUIRE(skid_owner &&
			skid_owner->transfer_type == PIGEN_TRANSFER_TYPE_SKID &&
			skid_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			skid_owner->module.index == module.index &&
			IS_INVALID_ID(skid_owner->transfer_argument));
		buf_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_BUF);
		REQUIRE(buf_descriptor && buf_descriptor->is_concrete &&
			buf_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE &&
			buf_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT);
		port_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_PORT);
		REQUIRE(port_descriptor && port_descriptor->is_concrete &&
			port_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE &&
			port_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER);
		fifo_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_FIFO);
		REQUIRE(fifo_descriptor && fifo_descriptor->is_concrete &&
			fifo_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_DEPTH &&
			fifo_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE);
		skid_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_SKID);
		REQUIRE(skid_descriptor && skid_descriptor->is_concrete &&
			skid_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_NONE &&
			skid_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_SKID_QUEUE);
		REQUIRE(pigen_expr_get(&sem_i, depth4) &&
			pigen_expr_constant(&sem_i, depth4).index != PIGEN_INVALID_ID);

		/* Case 1: DISTINCT INSTANCES. The declaration lowering reports
		 * success, the instance arena grows by exactly four, and the
		 * four newly published records are mutually distinct. The first
		 * `rc == 0` is green against the landed lowering; the
		 * declaration lowering reports success and the instance
		 * arena grows by exactly four. */
		instance_before = rtl_i.instance_count;
		REQUIRE(rtl_i.instances == NULL && instance_before == 0);
		rc = pigen_lower_rtl_module_declarations(&lowering_i, module);
		REQUIRE(rc == 0); /* Green against the landed lowering: the
		 * declaration lowering reports success. */
		exact_delta = rtl_i.instance_count == instance_before + 4;
		REQUIRE(exact_delta); /* Green: the landed lowering publishes four instances. */

		/* The four newly published records [instance_before ..
		 * instance_before+4) are MUTUALLY DISTINCT: no two of the four
		 * records are field-identical (origin, semantic_module,
		 * parameters, connections and module compared), so the set of
		 * newly published instance ids has cardinality exactly four and
		 * no instance id is published more than once. An implementation
		 * that shares one instance between two signals - or between all
		 * four - is caught here. */
		distinct = 1;
		{
			size_t k;
			for (k = 0; k + 1 < 4; k++) {
				const pigen_rtl_instance *r1 = &rtl_i.instances[k +
					instance_before];
				size_t l;
				for (l = k + 1; l < 4; l++) {
					const pigen_rtl_instance *r2 = &rtl_i.instances[l +
						instance_before];
					int identical =
						(r1->origin.source.index ==
							r2->origin.source.index) &&
						(r1->origin.start == r2->origin.start) &&
						(r1->origin.end == r2->origin.end) &&
						(r1->semantic_module.index ==
							r2->semantic_module.index) &&
						(r1->parameters.first == r2->parameters.first) &&
						(r1->parameters.count == r2->parameters.count) &&
						(r1->connections.first == r2->connections.first) &&
						(r1->connections.count == r2->connections.count) &&
						(r1->module.index == r2->module.index);
					distinct = distinct && !identical;
				}
			}
		}
		REQUIRE(distinct); /* Green: the four landed instances are mutually distinct. */

		pigen_rtl_lowering_free(&lowering_i);
		pigen_free_rtl_model(&rtl_i);
		pigen_free_semantic_model(&sem_i);
		pigen_free_sources(&sources_i);
	}

	/* (14) Storage FIFO depth: the PARAMETERIZED_QUEUE depth round-trip
	 * contract for pigen_lower_rtl_module_declarations (Task 6, "elastic
	 * slot / pulse / queue / skid -> corresponding primitive structure").
	 * The SAME four-signal storage module sections (12)/(13) build: one 8-bit
	 * unsigned data type, FOUR PIGEN_SEMANTIC_INTERNAL signals in one module -
	 * a PIGEN_TRANSFER_TYPE_BUF (ELASTIC_SLOT), a PIGEN_TRANSFER_TYPE_PORT
	 * (PULSE_REGISTER), a PIGEN_TRANSFER_TYPE_FIFO (PARAMETERIZED_QUEUE)
	 * whose transfer_argument is the constant 4 expression over that same
	 * 8-bit type (the owner's PIGEN_TRANSFER_PARAMETER_DEPTH requirement), and
	 * a PIGEN_TRANSFER_TYPE_SKID (SKID_QUEUE) - the three non-FIFO signals
	 * passing the exact invalid (pigen_expr_id){PIGEN_INVALID_ID}
	 * transfer-argument form the owner requires for
	 * PIGEN_TRANSFER_PARAMETER_NONE descriptors. Built through the owner APIs
	 * exactly as sections (12)/(13) do. Checked through
	 * pigen_rtl_lowering_init + pigen_lower_rtl_module_declarations + the RTL
	 * model's instance and expression arenas (the instance record is
	 * pigen_rtl_instance in include/pigen/rtl.h:111-117, whose ordered
	 * parameters range indexes rtl_i.instance_parameters):
	 *   Case 1: DEPTH ROUND-TRIP. (a) The call reports success - green
	 *     against the landed lowering (not a compile or harness error).
	 *     The FIFO depth round-trips exactly as the landed lowering
	 *     publishes it. (b) The FIFO signal's published
	 *     pigen_rtl_instance carries its semantic depth: the depth value 4
	 *     round-trips EXACTLY from the const-expr argument the test built,
	 *     witnessed on the parameter expression that sits in the instance's
	 *     ordered parameters record range and resolves through the landed
	 *     pigen_rtl_expr_get to a PIGEN_RTL_EXPR_INTEGER record whose value is
	 *     the interned const-expr value of the argument (4), not a spelling
	 *     match - and whose record is jointly width- and type-checked:
	 *     literal_bit_count 64 (the only literal width the landed lowering
	 *     owner path, pigen_rtl_expr_add_integer, can publish, so a
	 *     wrong-width literal carrying 4 cannot round-trip) plus a type
	 *     field resolving to the 8-bit unsigned witness record (width 8,
	 *     PIGEN_SIGN_UNSIGNED, the argument type's state domain), compared
	 *     by fields, not pointer. (c) The depth comes from the transfer argument, so no
	 *     PIGEN_TRANSFER_TYPE_* source enum spelling is matched - only the
	 *     realization (PARAMETERIZED_QUEUE) and its descriptor are consulted,
	 *     and the descriptor is ARGUMENT-sourced with no fixed_capacity.
	 *     (d) The three non-argument storage signals publish NO depth
	 *     parameter: their capacity is the descriptor's fixed_capacity
	 *     constant (ELASTIC_SLOT 1, PULSE_REGISTER 1, SKID_QUEUE 2), so each
	 *     of their instance records carries no parameter record that
	 *     resolves to that depth value.
	 * Exclusions (sibling sections, NOT pinned here): the instance-shape
	 * matrix (sibling section (12)), the distinct-instance publication and
	 * cross-contamination (sibling section (13)), and every arena count delta
	 * (sibling section storage-count-deltas); the primitive name each
	 * instance instantiates - there is no owner API resolving a
	 * pigen_rtl_module_id to a primitive definition name, so that mapping is
	 * an implementation design decision, not an owner-checkable fact here.
	 * Green: every owner-construction and descriptor assert passes
	 * against the landed owners; the declaration lowering (Case 1's
	 * `rc == 0`) and every later assert in this section pass against the
	 * landed lowering. No stale red marker remains in this file. */
	SECTION("t6-storage-fifo-depth") {
		const char *text =
			"module store : buf a ; port b ; fifo c ; skid d ;\n";
		pigen_source_manager sources_j = {0};
		pigen_semantic_model sem_j;
		pigen_rtl_model rtl_j = {0};
		pigen_rtl_lowering lowering_j;
		pigen_data_type_id unsized;
		pigen_data_type_id t8;
		pigen_const_expr_id width8;
		pigen_expr_id depth4;
		pigen_const_expr_id depth4_const;
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name_a;
		pigen_source_span name_b;
		pigen_source_span name_c;
		pigen_source_span name_d;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id buf_symbol;
		pigen_symbol_id port_symbol;
		pigen_symbol_id fifo_symbol;
		pigen_symbol_id skid_symbol;
		pigen_module_id module;
		pigen_signal_id buf;
		pigen_signal_id port;
		pigen_signal_id fifo;
		pigen_signal_id skid;
		const pigen_semantic_signal *buf_owner;
		const pigen_semantic_signal *fifo_owner;
		const pigen_transfer_type_descriptor *fifo_descriptor;
		const pigen_transfer_realization_descriptor *queue_descriptor;
		const pigen_transfer_realization_descriptor *slot_descriptor;
		const pigen_transfer_realization_descriptor *pulse_descriptor;
		const pigen_transfer_realization_descriptor *skid_descriptor;
		uint64_t expected_depth;
		size_t instance_before;
		const pigen_rtl_instance *fifo_instance;
		const pigen_rtl_instance *buf_instance;
		const pigen_rtl_instance *port_instance;
		const pigen_rtl_instance *skid_instance;
		size_t fifo_parameter_count;
		int fifo_has_depth;
		int buf_has_depth;
		int port_has_depth;
		int skid_has_depth;
		int rc;

		/* Build the storage module through the owner APIs (section (12)'s
		 * landed construction pattern, four INTERNAL signals of the same
		 * 8-bit type): the type first, then the depth argument over that
		 * type, a source file, the compilation scope, a module in it, and
		 * the four declared signals. The module symbol's declaration
		 * spans the whole file (as module_add requires), the module scope
		 * uses the same span, and each signal symbol's name span is
		 * contained in and its declaration span equal to the span
		 * signal_add checks. */
		pigen_semantic_init(&sem_j, &sources_j);
		pigen_rtl_lowering_init(&lowering_j, &sem_j, &rtl_j);
		unsized = pigen_data_type_unsized_integer(&sem_j);
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem_j, 8, unsized);
		t8 = pigen_data_type_unsigned_integer(&sem_j, width8);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(t8));
		source = pigen_source_add(&sources_j,
			"lower_storage_fifo_depth.pigen", text, strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name_a = (pigen_source_span){source, 19, 20}; /* "a" */
		name_b = (pigen_source_span){source, 28, 29}; /* "b" */
		name_c = (pigen_source_span){source, 37, 38}; /* "c" */
		name_d = (pigen_source_span){source, 46, 47}; /* "d" */
		/* The FIFO depth: a real expression over the interned constant 4
		 * of the 8-bit type - the valid constant-expression identity the
		 * owner requires for PIGEN_TRANSFER_PARAMETER_DEPTH descriptors. */
		depth4 = pigen_expr_add_integer(&sem_j, 4, t8, name_c);
		REQUIRE(!IS_INVALID_ID(depth4));
		sem_j.compilation_scope = pigen_scope_add(&sem_j,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem_j.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_j, sem_j.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem_j, sem_j.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem_j, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_j, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_a, name_a, &buf_symbol, NULL) == PIGEN_DECLARE_OK);
		buf = pigen_signal_add(&sem_j, (pigen_syntax_id){2}, module,
			buf_symbol, t8, pigen_semantic_scalar_shape(&sem_j),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
			PIGEN_SEMANTIC_INTERNAL, name_a);
		REQUIRE(buf.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_j, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_b, name_b, &port_symbol, NULL) == PIGEN_DECLARE_OK);
		port = pigen_signal_add(&sem_j, (pigen_syntax_id){3}, module,
			port_symbol, t8, pigen_semantic_scalar_shape(&sem_j),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_PORT,
			PIGEN_SEMANTIC_INTERNAL, name_b);
		REQUIRE(port.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_j, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_c, name_c, &fifo_symbol, NULL) == PIGEN_DECLARE_OK);
		fifo = pigen_signal_add(&sem_j, (pigen_syntax_id){4}, module,
			fifo_symbol, t8, pigen_semantic_scalar_shape(&sem_j),
			depth4, PIGEN_TRANSFER_TYPE_FIFO,
			PIGEN_SEMANTIC_INTERNAL, name_c);
		REQUIRE(fifo.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_j, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_d, name_d, &skid_symbol, NULL) == PIGEN_DECLARE_OK);
		skid = pigen_signal_add(&sem_j, (pigen_syntax_id){5}, module,
			skid_symbol, t8, pigen_semantic_scalar_shape(&sem_j),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_SKID,
			PIGEN_SEMANTIC_INTERNAL, name_d);
		REQUIRE(skid.index != PIGEN_INVALID_ID);

		/* Owner facts the contract rides on (all green against the landed
		 * owners): the four signal records report their transfer type and
		 * the INTERNAL direction; only FIFO carries a transfer argument -
		 * the constant 4 expression over the 8-bit type (a valid
		 * constant-expression identity the owner requires for
		 * PIGEN_TRANSFER_PARAMETER_DEPTH) - and its descriptor is the
		 * concrete FIFO mapped to PARAMETERIZED_QUEUE with the DEPTH
		 * parameter; the other three carry the invalid argument and map to
		 * fixed-capacity realizations. The interned value of the built
		 * argument is 4, so the depth witness below compares the RTL
		 * record against the interned const-expr value, not a spelling. */
		buf_owner = pigen_signal_get(&sem_j, buf);
		REQUIRE(buf_owner &&
			buf_owner->transfer_type == PIGEN_TRANSFER_TYPE_BUF &&
			buf_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			IS_INVALID_ID(buf_owner->transfer_argument));
		fifo_owner = pigen_signal_get(&sem_j, fifo);
		REQUIRE(fifo_owner &&
			fifo_owner->transfer_type == PIGEN_TRANSFER_TYPE_FIFO &&
			fifo_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			fifo_owner->transfer_argument.index == depth4.index);
		fifo_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_FIFO);
		REQUIRE(fifo_descriptor && fifo_descriptor->is_concrete &&
			fifo_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_DEPTH &&
			fifo_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE);
		queue_descriptor =
			pigen_transfer_realization_descriptor_get(
				PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE);
		REQUIRE(queue_descriptor &&
			queue_descriptor->capacity_source ==
				PIGEN_TRANSFER_CAPACITY_ARGUMENT &&
			queue_descriptor->ready_dependency ==
				PIGEN_TRANSFER_READY_OCCUPANCY &&
			queue_descriptor->has_occupancy == 1 &&
			queue_descriptor->reset == PIGEN_TRANSFER_RESET_EMPTY);
		slot_descriptor =
			pigen_transfer_realization_descriptor_get(
				PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT);
		REQUIRE(slot_descriptor &&
			slot_descriptor->capacity_source ==
				PIGEN_TRANSFER_CAPACITY_FIXED &&
			slot_descriptor->fixed_capacity == 1 &&
			slot_descriptor->ready_dependency ==
				PIGEN_TRANSFER_READY_DOWNSTREAM &&
			slot_descriptor->has_occupancy == 1 &&
			slot_descriptor->reset == PIGEN_TRANSFER_RESET_EMPTY);
		pulse_descriptor =
			pigen_transfer_realization_descriptor_get(
				PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER);
		REQUIRE(pulse_descriptor &&
			pulse_descriptor->capacity_source ==
				PIGEN_TRANSFER_CAPACITY_FIXED &&
			pulse_descriptor->fixed_capacity == 1 &&
			pulse_descriptor->ready_dependency ==
				PIGEN_TRANSFER_READY_CONSTANT &&
			pulse_descriptor->has_occupancy == 0 &&
			pulse_descriptor->reset == PIGEN_TRANSFER_RESET_EMPTY);
		skid_descriptor =
			pigen_transfer_realization_descriptor_get(
				PIGEN_TRANSFER_REALIZATION_SKID_QUEUE);
		REQUIRE(skid_descriptor &&
			skid_descriptor->capacity_source ==
				PIGEN_TRANSFER_CAPACITY_FIXED &&
			skid_descriptor->fixed_capacity == 2 &&
			skid_descriptor->ready_dependency ==
				PIGEN_TRANSFER_READY_OCCUPANCY &&
			skid_descriptor->has_occupancy == 1 &&
			skid_descriptor->reset == PIGEN_TRANSFER_RESET_EMPTY);
		depth4_const = pigen_expr_constant(&sem_j, depth4);
		REQUIRE(!IS_INVALID_ID(depth4_const));
		expected_depth = 0;
		REQUIRE(pigen_const_expr_evaluate_u64(&sem_j, depth4_const,
			&expected_depth) && expected_depth == 4);

		/* Case 1: DEPTH ROUND-TRIP. The declaration lowering reports
		 * success, the FIFO instance carries the depth argument's interned
		 * value as a parameter record, and the three fixed-capacity
		 * instances carry no depth parameter. The first `rc == 0` is
		 * green against the landed lowering, which reports success and
		 * publishes the FIFO depth parameter record; the three
		 * fixed-capacity instances carry no depth parameter. */
		instance_before = rtl_j.instance_count;
		REQUIRE(rtl_j.instances == NULL && instance_before == 0);
		rc = pigen_lower_rtl_module_declarations(&lowering_j, module);
		REQUIRE(rc == 0); /* Green against the landed lowering: the
		 * declaration lowering reports success. */

		/* The four storage instances are published at the four new arena
		 * slots in declaration order: BUF, PORT, FIFO, SKID. (Section (13)
		 * pins that they are mutually distinct; here we only need the four
		 * records to inspect their parameters ranges.) */
		fifo_instance = &rtl_j.instances[instance_before + 2];
		buf_instance = &rtl_j.instances[instance_before + 0];
		port_instance = &rtl_j.instances[instance_before + 1];
		skid_instance = &rtl_j.instances[instance_before + 3];

		/* (b) The FIFO instance's ordered parameters range holds a parameter
		 * expression that resolves to the interned const-expr value of the
		 * transfer argument (expected_depth, 4) - the depth round-trips
		 * EXACTLY, witnessed on the record value, not a source spelling.
		 * The witness record is jointly width- and type-checked against the
		 * landed owners: the depth argument was built over the 8-bit type
		 * (pigen_expr_add_integer(4, t8, ...)), and the ONLY expression
		 * lowering owner path, pigen_lower_rtl_expression, lowers exact
		 * integers through pigen_rtl_expr_add_integer - the uint64_t
		 * convenience constructor that hardcodes literal_bit_count 64. So
		 * the record a correct implementation publishes is 8-bit TYPED with
		 * a 64-bit literal: (i) literal_bit_count == 64 - the only literal
		 * width the owner path can publish (a 1-bit, 8-bit or other-width
		 * literal carrying value 4 cannot round-trip) - and (ii) its type
		 * field resolves through pigen_rtl_type_get to a record with
		 * width == 8, signedness == PIGEN_SIGN_UNSIGNED and
		 * state_domain == the 8-bit witness type's domain, compared field
		 * by field, never by pointer (a distinct but equal type record
		 * still passes). */
		fifo_parameter_count = fifo_instance->parameters.count;
		fifo_has_depth = 0;
		for (size_t p = 0; p < fifo_parameter_count; p++) {
			const pigen_rtl_expr *pe = pigen_rtl_expr_get(&rtl_j,
				rtl_j.instance_parameters[fifo_instance->parameters.first +
					p]);
			const pigen_rtl_type *pe_type;
			if (!pe || pe->kind != PIGEN_RTL_EXPR_INTEGER ||
				pe->value != expected_depth)
				continue;
			if (pe->literal_bit_count != 64) /* (i) owner-pinned literal width */
				continue;
			pe_type = pigen_rtl_type_get(&rtl_j, pe->type);
			if (!pe_type)
				continue;
			if (pe_type->width != 8 ||
				pe_type->signedness != PIGEN_SIGN_UNSIGNED ||
				pe_type->state_domain !=
					pigen_data_type_state_domain(&sem_j, t8)) /* (ii) 8-bit type witness */
				continue;
			fifo_has_depth = 1;
		}
		REQUIRE(fifo_has_depth); /* Green: the landed lowering publishes the
		 * FIFO depth parameter record. */

		/* (c) The depth is sourced from the transfer argument, not a
		 * spelling match: the only realization consulted is
		 * PARAMETERIZED_QUEUE (the descriptor asserted above), which is
		 * ARGUMENT-sourced and carries no fixed capacity to substitute. */
		REQUIRE(fifo_descriptor->realization ==
			PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE);
		REQUIRE(queue_descriptor->capacity_source ==
			PIGEN_TRANSFER_CAPACITY_ARGUMENT);

		/* (d) The three non-argument storage signals publish NO depth
		 * parameter: their capacity is the descriptor's fixed_capacity
		 * constant (ELASTIC_SLOT 1, PULSE_REGISTER 1, SKID_QUEUE 2), so
		 * none of their parameter records resolves to the depth value. */
		buf_has_depth = 0;
		for (size_t p = 0; p < buf_instance->parameters.count; p++) {
			const pigen_rtl_expr *pe = pigen_rtl_expr_get(&rtl_j,
				rtl_j.instance_parameters[buf_instance->parameters.first +
					p]);
			if (pe && pe->kind == PIGEN_RTL_EXPR_INTEGER &&
				pe->value == expected_depth)
				buf_has_depth = 1;
		}
		REQUIRE(!buf_has_depth); /* Green: a fixed-capacity realization
		 * publishes no depth parameter. */
		port_has_depth = 0;
		for (size_t p = 0; p < port_instance->parameters.count; p++) {
			const pigen_rtl_expr *pe = pigen_rtl_expr_get(&rtl_j,
				rtl_j.instance_parameters[port_instance->parameters.first +
					p]);
			if (pe && pe->kind == PIGEN_RTL_EXPR_INTEGER &&
				pe->value == expected_depth)
				port_has_depth = 1;
		}
		REQUIRE(!port_has_depth); /* Green: no depth parameter. */
		skid_has_depth = 0;
		for (size_t p = 0; p < skid_instance->parameters.count; p++) {
			const pigen_rtl_expr *pe = pigen_rtl_expr_get(&rtl_j,
				rtl_j.instance_parameters[skid_instance->parameters.first +
					p]);
			if (pe && pe->kind == PIGEN_RTL_EXPR_INTEGER &&
				pe->value == expected_depth)
				skid_has_depth = 1;
		}
		REQUIRE(!skid_has_depth); /* Green: no depth parameter. */

		pigen_rtl_lowering_free(&lowering_j);
		pigen_free_rtl_model(&rtl_j);
		pigen_free_semantic_model(&sem_j);
		pigen_free_sources(&sources_j);
	}

	/* (15) Storage count deltas: the STORAGE COUNT-DELTA and IDEMPOTENCE
	 * contract for pigen_lower_rtl_module_declarations (Task 6, "elastic slot /
	 * pulse / queue / skid -> corresponding primitive structure"). Sibling of
	 * sections (12) (matrix), (13) (instances) and (14) (FIFO depth): one
	 * module holds FOUR INTERNAL signals of the SAME 8-bit unsigned data type,
	 * a PIGEN_TRANSFER_TYPE_BUF (ELASTIC_SLOT), a PIGEN_TRANSFER_TYPE_PORT
	 * (PULSE_REGISTER), a PIGEN_TRANSFER_TYPE_FIFO (PARAMETERIZED_QUEUE) whose
	 * transfer_argument is a constant 4 expression of the same type, and a
	 * PIGEN_TRANSFER_TYPE_SKID (SKID_QUEUE). The three non-FIFO signals pass
	 * the exact invalid transfer-argument form sections (8)-(14) use. Built
	 * through the owner APIs exactly as section (8)'s landed construction
	 * pattern: a source file, the compilation scope, a module symbol +
	 * pigen_module_add, a PIGEN_SYMBOL_SIGNAL declaration per signal (matching
	 * data type and declaration span) and pigen_signal_add with
	 * PIGEN_SEMANTIC_INTERNAL - permitted for these concrete types.
	 *   Case 1: COUNT DELTAS. Snapshot all THREE arenas (object_count,
	 *     expression_count, instance_count) BEFORE the first
	 *     pigen_lower_rtl_module_declarations call; the call reports success
	 *     rc == 0, green against the landed lowering, NOT a compile/harness
	 *     error. The landed lowering reports success and the declaration
	 *     lowering publishes the four storage signals. Then:
	 *     (a) the OBJECT-arena delta is EXACTLY four - one VARIABLE payload
	 *         object per storage signal (section (12)'s payload shape), and no
	 *         other objects are published by the declaration lowering for
	 *         these four signals (the storage realizations publish no control
	 *         objects, so a spurious control-object publish or a shared payload
	 *         is caught here);
	 *     (b) the INSTANCE-arena delta is EXACTLY four - one pigen_rtl_instance
	 *         per storage signal (BUF, PORT, FIFO, SKID each own their
	 *         primitive instance, section (13));
	 *     (c) the EXPRESSION-arena delta is bounded and pinned from evidence,
	 *         NOT guessed, to the inclusive range [0, 1]. The derivation, by
	 *         emulating the landed memo/interning behavior (as section (11)
	 *         did with its [2,4] bound):
	 *         - The payload type publishes NO expression record: the landed
	 *           pigen_lower_rtl_type (src/rtl_lower.c:81) stores a scalar
	 *           type's width as an evaluated uint64_t and keeps its
	 *           width_expression invalid (src/rtl_lower.c:148-152), so
	 *           lowering the 8-bit type interns nothing into the expression
	 *           arena (and the snapshot below is taken after that memo, the
	 *           section (11) convention).
	 *         - The storage realizations publish NO constant CONTROL
	 *           expression: unlike the net/variable family (section (11)),
	 *           whose valid/ready are owner-published 1-bit descriptor
	 *           constants, the storage valid/ready are driven by the
	 *           primitive (downstream / occupancy / always-ready), so no
	 *           1-bit control constant is interned per storage signal.
	 *         - The ONLY expression the lowering may intern for these four
	 *           signals is the FIFO's depth argument: a correct implementation
	 *           publishes the transfer argument's interned const-expr value as
	 *           the FIFO instance's depth parameter record (the record value
	 *           section (14) pins as PIGEN_RTL_EXPR_INTEGER == 4). The landed
	 *           pigen_lower_rtl_expression memo interns ONCE per const-expr
	 *           IDENTITY (src/rtl_lower.c:283), so that one argument identity
	 *           yields at most one expression record.
	 *         Hence the emulated-correct delta is EXACTLY 1 (the shared
	 *         depth-4 record), the range [0, 1] spans every plausible
	 *         interning variant (0 if a depth-carrying implementation stores
	 *         the value in the record without a dedicated expression slot, 1
	 *         with one shared record), and an implementation that interns a
	 *         per-signal or duplicated depth record (delta > 1) is caught here.
	 *     (d) the endpoints map grows past all four signal indices.
	 *   Case 2: IDEMPOTENCE. A SECOND call of
	 *     pigen_lower_rtl_module_declarations on the SAME lowering and module
	 *     publishes NOTHING new: object_count, expression_count and
	 *     instance_count are all unchanged, the endpoints-map count is
	 *     unchanged, and each of the four signals' endpoints record is
	 *     field-identical to after the first call (all six fields per signal
	 *     compared, as section (11) does).
	 * Exclusions (sibling sections, NOT re-asserted here): the instance-shape
	 * matrix (section (12)), the distinct-instance publication and
	 * cross-contamination (section (13)), and the FIFO depth parameter
	 * round-trip and descriptor facts (section (14)).
	 * Green: every owner-construction and descriptor assert passes
	 * against the landed owners (including the already-implemented
	 * pigen_lower_rtl_type memo, which resolves the 8-bit type with no
	 * expression publish); the declaration lowering (Case 1's `rc == 0`)
	 * and every later assert in this section pass against the landed
	 * lowering, which reports success. No stale red marker remains
	 * in this file. */
	SECTION("t6-storage-counts") {
		const char *text =
			"module store : buf a ; port b ; fifo c ; skid d ;\n";
		pigen_source_manager sources_k = {0};
		pigen_semantic_model sem_k;
		pigen_rtl_model rtl_k = {0};
		pigen_rtl_lowering lowering_k;
		pigen_data_type_id unsized;
		pigen_data_type_id t8;
		pigen_const_expr_id width8;
		pigen_expr_id depth4;
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name_a;
		pigen_source_span name_b;
		pigen_source_span name_c;
		pigen_source_span name_d;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id buf_symbol;
		pigen_symbol_id port_symbol;
		pigen_symbol_id fifo_symbol;
		pigen_symbol_id skid_symbol;
		pigen_module_id module;
		pigen_signal_id buf;
		pigen_signal_id port;
		pigen_signal_id fifo;
		pigen_signal_id skid;
		const pigen_semantic_signal *buf_owner;
		const pigen_semantic_signal *fifo_owner;
		const pigen_transfer_type_descriptor *fifo_descriptor;
		pigen_rtl_type_id lowered_t8;
		const pigen_rtl_type *t8_record;
		pigen_rtl_signal_endpoints buf_record;
		pigen_rtl_signal_endpoints port_record;
		pigen_rtl_signal_endpoints fifo_record;
		pigen_rtl_signal_endpoints skid_record;
		pigen_rtl_signal_endpoints buf_again;
		pigen_rtl_signal_endpoints port_again;
		pigen_rtl_signal_endpoints fifo_again;
		pigen_rtl_signal_endpoints skid_again;
		size_t object_before;
		size_t expression_before;
		size_t instance_before;
		size_t object_delta;
		size_t expression_delta;
		size_t instance_delta;
		size_t endpoint_map_before;
		int count_deltas;
		int idempotent;
		int rc;

		/* Build the storage module through the owner APIs (section (8)'s
		 * landed construction pattern, four INTERNAL signals of the same
		 * 8-bit type): the type first, then the depth argument over that
		 * type, a source file, the compilation scope, a module in it, and the
		 * four declared signals. The module symbol's declaration spans the
		 * whole file (as module_add requires), the module scope uses the same
		 * span, and each signal symbol's name span is contained in and its
		 * declaration span equal to the span signal_add checks. */
		pigen_semantic_init(&sem_k, &sources_k);
		pigen_rtl_lowering_init(&lowering_k, &sem_k, &rtl_k);
		unsized = pigen_data_type_unsized_integer(&sem_k);
		REQUIRE(!IS_INVALID_ID(unsized));
		width8 = pigen_const_expr_intern_integer(&sem_k, 8, unsized);
		t8 = pigen_data_type_unsigned_integer(&sem_k, width8);
		REQUIRE(!IS_INVALID_ID(width8) && !IS_INVALID_ID(t8));
		source = pigen_source_add(&sources_k,
			"lower_storage_count_deltas.pigen", text, strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name_a = (pigen_source_span){source, 19, 20}; /* "a" */
		name_b = (pigen_source_span){source, 28, 29}; /* "b" */
		name_c = (pigen_source_span){source, 37, 38}; /* "c" */
		name_d = (pigen_source_span){source, 46, 47}; /* "d" */
		/* The FIFO depth: a real expression over the interned constant 4 of
		 * the 8-bit type - the valid constant-expression identity the owner
		 * requires for PIGEN_TRANSFER_PARAMETER_DEPTH descriptors. */
		depth4 = pigen_expr_add_integer(&sem_k, 4, t8, name_c);
		REQUIRE(!IS_INVALID_ID(depth4));
		sem_k.compilation_scope = pigen_scope_add(&sem_k,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem_k.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_k, sem_k.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem_k, sem_k.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem_k, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_k, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_a, name_a, &buf_symbol, NULL) == PIGEN_DECLARE_OK);
		buf = pigen_signal_add(&sem_k, (pigen_syntax_id){2}, module,
			buf_symbol, t8, pigen_semantic_scalar_shape(&sem_k),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
			PIGEN_SEMANTIC_INTERNAL, name_a);
		REQUIRE(buf.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_k, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_b, name_b, &port_symbol, NULL) == PIGEN_DECLARE_OK);
		port = pigen_signal_add(&sem_k, (pigen_syntax_id){3}, module,
			port_symbol, t8, pigen_semantic_scalar_shape(&sem_k),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_PORT,
			PIGEN_SEMANTIC_INTERNAL, name_b);
		REQUIRE(port.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_k, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_c, name_c, &fifo_symbol, NULL) == PIGEN_DECLARE_OK);
		fifo = pigen_signal_add(&sem_k, (pigen_syntax_id){4}, module,
			fifo_symbol, t8, pigen_semantic_scalar_shape(&sem_k),
			depth4, PIGEN_TRANSFER_TYPE_FIFO,
			PIGEN_SEMANTIC_INTERNAL, name_c);
		REQUIRE(fifo.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_k, module_scope, PIGEN_SYMBOL_SIGNAL,
			t8, name_d, name_d, &skid_symbol, NULL) == PIGEN_DECLARE_OK);
		skid = pigen_signal_add(&sem_k, (pigen_syntax_id){5}, module,
			skid_symbol, t8, pigen_semantic_scalar_shape(&sem_k),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_SKID,
			PIGEN_SEMANTIC_INTERNAL, name_d);
		REQUIRE(skid.index != PIGEN_INVALID_ID);

		/* Owner facts the contract rides on (all green against the landed
		 * owners): the four signal records report their transfer type and the
		 * INTERNAL direction; only FIFO carries a transfer argument - the
		 * constant 4 expression over the 8-bit type - and its descriptor is
		 * the concrete FIFO mapped to PARAMETERIZED_QUEUE; the other three
		 * carry the invalid argument. The already-implemented
		 * pigen_lower_rtl_type memo resolves the 8-bit data type to a record
		 * in the RTL model and publishes NO expression (a scalar type stores
		 * its width as a uint64_t), so the arena snapshots below - taken
		 * after that memo, the section (11) convention - are unaffected by it. */
		buf_owner = pigen_signal_get(&sem_k, buf);
		REQUIRE(buf_owner &&
			buf_owner->transfer_type == PIGEN_TRANSFER_TYPE_BUF &&
			buf_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			IS_INVALID_ID(buf_owner->transfer_argument));
		fifo_owner = pigen_signal_get(&sem_k, fifo);
		REQUIRE(fifo_owner &&
			fifo_owner->transfer_type == PIGEN_TRANSFER_TYPE_FIFO &&
			fifo_owner->direction == PIGEN_SEMANTIC_INTERNAL &&
			fifo_owner->transfer_argument.index == depth4.index);
		fifo_descriptor =
			pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_FIFO);
		REQUIRE(fifo_descriptor && fifo_descriptor->is_concrete &&
			fifo_descriptor->realization ==
				PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE);
		lowered_t8 = pigen_lower_rtl_type(&lowering_k, t8);
		REQUIRE(!IS_INVALID_ID(lowered_t8));
		t8_record = pigen_rtl_type_get(&rtl_k, lowered_t8);
		REQUIRE(t8_record);

		/* Case 1: COUNT DELTAS. Snapshot all three arenas BEFORE the first
		 * declaration call; the first `rc == 0` is green against the
		 * landed lowering.
		 * The storage realizations own their primitive: exactly one payload
		 * object and exactly one instance per signal, and at most the shared
		 * FIFO depth argument as the sole interned expression. */
		object_before = rtl_k.object_count;
		expression_before = rtl_k.expression_count;
		instance_before = rtl_k.instance_count;
		REQUIRE(instance_before == 0); /* no instances before the call */
		rc = pigen_lower_rtl_module_declarations(&lowering_k, module);
		REQUIRE(rc == 0); /* Green against the landed lowering: the
		 * declaration lowering reports success. */
		REQUIRE(lowering_k.lowered_endpoint_count > buf.index);
		REQUIRE(lowering_k.lowered_endpoint_count > port.index);
		REQUIRE(lowering_k.lowered_endpoint_count > fifo.index);
		REQUIRE(lowering_k.lowered_endpoint_count > skid.index);
		object_delta = rtl_k.object_count - object_before;
		expression_delta = rtl_k.expression_count - expression_before;
		instance_delta = rtl_k.instance_count - instance_before;
		/* (a)+(b)+(c): exactly one VARIABLE payload object per signal (four),
		 * exactly one pigen_rtl_instance per signal (four), and the
		 * expression delta bounded by the shared depth argument (at most one,
		 * per the [0,1] derivation above). A control-object publish, a shared
		 * or missing payload, a missing or duplicated instance, or a
		 * per-signal/duplicated depth record is each caught here. */
		/* The [0,1] range: the lower bound 0 is implicit for the unsigned
		 * delta; the upper bound 1 pins the single shared depth record. */
		count_deltas = object_delta == 4 && instance_delta == 4 &&
			expression_delta <= 1;
		REQUIRE(count_deltas); /* Green: the landed lowering publishes the count deltas. */

		/* Case 2: IDEMPOTENCE. A second call on the SAME lowering and module
		 * publishes NOTHING new: all three arena counts and the endpoints-map
		 * count are unchanged and each of the four signals' endpoints record
		 * is field-identical to after the first call (all six fields per
		 * signal). Green against the landed lowering, which republishes
		 * nothing on the second pass. */
		buf_record = lowering_k.lowered_endpoints[buf.index];
		port_record = lowering_k.lowered_endpoints[port.index];
		fifo_record = lowering_k.lowered_endpoints[fifo.index];
		skid_record = lowering_k.lowered_endpoints[skid.index];
		object_before = rtl_k.object_count;
		expression_before = rtl_k.expression_count;
		instance_before = rtl_k.instance_count;
		endpoint_map_before = lowering_k.lowered_endpoint_count;
		rc = pigen_lower_rtl_module_declarations(&lowering_k, module);
		REQUIRE(rc == 0); /* Green against the landed lowering. */
		buf_again = lowering_k.lowered_endpoints[buf.index];
		port_again = lowering_k.lowered_endpoints[port.index];
		fifo_again = lowering_k.lowered_endpoints[fifo.index];
		skid_again = lowering_k.lowered_endpoints[skid.index];
		idempotent = rtl_k.object_count == object_before &&
			rtl_k.expression_count == expression_before &&
			rtl_k.instance_count == instance_before &&
			lowering_k.lowered_endpoint_count == endpoint_map_before &&
			(buf_again.payload.index == buf_record.payload.index) &&
			(buf_again.valid.index == buf_record.valid.index) &&
			(buf_again.ready.index == buf_record.ready.index) &&
			(buf_again.input_payload.index == buf_record.input_payload.index) &&
			(buf_again.input_valid.index == buf_record.input_valid.index) &&
			(buf_again.input_ready.index == buf_record.input_ready.index) &&
			(port_again.payload.index == port_record.payload.index) &&
			(port_again.valid.index == port_record.valid.index) &&
			(port_again.ready.index == port_record.ready.index) &&
			(port_again.input_payload.index == port_record.input_payload.index) &&
			(port_again.input_valid.index == port_record.input_valid.index) &&
			(port_again.input_ready.index == port_record.input_ready.index) &&
			(fifo_again.payload.index == fifo_record.payload.index) &&
			(fifo_again.valid.index == fifo_record.valid.index) &&
			(fifo_again.ready.index == fifo_record.ready.index) &&
			(fifo_again.input_payload.index == fifo_record.input_payload.index) &&
			(fifo_again.input_valid.index == fifo_record.input_valid.index) &&
			(fifo_again.input_ready.index == fifo_record.input_ready.index) &&
			(skid_again.payload.index == skid_record.payload.index) &&
			(skid_again.valid.index == skid_record.valid.index) &&
			(skid_again.ready.index == skid_record.ready.index) &&
			(skid_again.input_payload.index == skid_record.input_payload.index) &&
			(skid_again.input_valid.index == skid_record.input_valid.index) &&
			(skid_again.input_ready.index == skid_record.input_ready.index);
		REQUIRE(idempotent); /* Green: the landed lowering is idempotent. */

		pigen_rtl_lowering_free(&lowering_k);
		pigen_free_rtl_model(&rtl_k);
		pigen_free_semantic_model(&sem_k);
		pigen_free_sources(&sources_k);
	}

	/* (16) Task 8 fire-identity gates. The section builds the valid owner
	 * witness(es) through the owner APIs (one clock domain per module,
	 * processes and transfers with the owner-span constraints - the process
	 * span contains the clock expression's span and the transfer span is
	 * contained in the process span), asserts the witnesses are valid, and
	 * pins the owner-level enumeration-accessor contract GREEN: the accessors
	 * enumerate exactly the owner's records in arena (addition) order,
	 * including a module with a second process and a second transfer and a
	 * valid module with zero processes (NULL/0 for an invalid or empty
	 * owner). It then STAGES RED at the first behavioral assert: the valid
	 * single-transfer owner witness now lowers, so
	 * pigen_lower_rtl_transfers(&lowering, module) must return 0. The
	 * unimplemented entry point currently returns -1, so that assert fails
	 * here. Every fire-identity gate family is staged RED behind that first
	 * red and only needs to compile until the lowering lands; nothing
	 * anywhere asserts that a failed call left the model or the maps
	 * unchanged (a failed compile may leave partial state behind). */
	SECTION("t8-skeleton") {
		/* The base witness source: one clock plus two data signals, so the
		 * (0) accessor coverage can add a second transfer on a distinct
		 * destination payload. The (1)-(5) RED families build their own
		 * isolated modules on their own sources below the first red. */
		const char *text =
			"module top : input clk : logic ; input d : logic ; input e : logic ;\n";
		pigen_source_manager sources_t = {0};
		pigen_semantic_model sem_t;
		pigen_rtl_model rtl_t = {0};
		pigen_rtl_lowering lowering_t;
		pigen_data_type_id t1;
		pigen_const_expr_id width1;
		pigen_source_id source;
		pigen_source_span whole;
		pigen_source_span name_clk;
		pigen_source_span name_d;
		pigen_source_span name_e;
		pigen_source_span process_span;
		pigen_source_span process_span2;
		pigen_source_span transfer_span;
		pigen_source_span transfer_span2;
		pigen_scope_id module_scope;
		pigen_symbol_id module_symbol;
		pigen_symbol_id clk_symbol;
		pigen_symbol_id d_symbol;
		pigen_symbol_id e_symbol;
		pigen_module_id module;
		pigen_module_id module_empty;
		pigen_signal_id clk;
		pigen_signal_id d;
		pigen_signal_id e;
		pigen_clock_domain_id domain;
		pigen_expr_id clk_expr;
		pigen_process_id process;
		pigen_process_id process2;
		pigen_lvalue_id dest;
		pigen_lvalue_id dest2;
		pigen_expr_id value;
		pigen_expr_id value2;
		pigen_transfer_id transfer;
		pigen_transfer_id transfer2;
		const pigen_semantic_process *owner_process;
		const pigen_semantic_transfer *owner_transfer;
		const pigen_process_id *module_processes;
		const pigen_transfer_id *process_transfers;
		size_t module_process_count;
		size_t process_transfer_count;
		/* RED-only introspection handles shared by the (1)-(5) gate families,
		 * built after the first red so they never run until the lowering
		 * lands. */
		size_t i, j;
		const pigen_rtl_update *upd;
		const pigen_rtl_equation *eq;
		const pigen_rtl_process *rtlp;

		/* Build the witness through the owner APIs (the section 8 pattern):
		 * a 1-bit data type, a source file, the compilation scope, a module,
		 * a declared clock signal and a declared data signal. The spans hold
		 * the owner constraints: the module symbol's declaration spans the
		 * whole file and the module scope uses the same span; each signal's
		 * name span is contained in (and equal to) its declaration span. */
		pigen_semantic_init(&sem_t, &sources_t);
		pigen_rtl_lowering_init(&lowering_t, &sem_t, &rtl_t);
		width1 = pigen_const_expr_intern_integer(&sem_t, 1,
			pigen_data_type_unsized_integer(&sem_t));
		t1 = pigen_data_type_sized_logic(&sem_t, 1, PIGEN_SIGN_UNSIGNED);
		REQUIRE(!IS_INVALID_ID(width1) && !IS_INVALID_ID(t1));
		source = pigen_source_add(&sources_t, "lower_transfers_shape.pigen",
			text, strlen(text));
		REQUIRE(source.index != PIGEN_INVALID_ID);
		whole = (pigen_source_span){source, 0, strlen(text)};
		name_clk = (pigen_source_span){source, 19, 22}; /* "clk" */
		name_d = (pigen_source_span){source, 39, 40}; /* "d" */
		name_e = (pigen_source_span){source, 57, 58}; /* "e" */
		process_span = (pigen_source_span){source, 13, 40};
		process_span2 = (pigen_source_span){source, 13, 60};
		transfer_span = (pigen_source_span){source, 33, 40};
		transfer_span2 = (pigen_source_span){source, 33, 40};
		sem_t.compilation_scope = pigen_scope_add(&sem_t,
			(pigen_scope_id){PIGEN_INVALID_ID},
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		REQUIRE(sem_t.compilation_scope.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_t, sem_t.compilation_scope,
			PIGEN_SYMBOL_MODULE,
			(pigen_data_type_id){PIGEN_INVALID_ID},
			whole, whole, &module_symbol, NULL) == PIGEN_DECLARE_OK);
		module_scope = pigen_scope_add(&sem_t, sem_t.compilation_scope, whole);
		REQUIRE(module_scope.index != PIGEN_INVALID_ID);
		module = pigen_module_add(&sem_t, (pigen_syntax_id){1}, module_symbol,
			module_scope, whole);
		REQUIRE(module.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_t, module_scope, PIGEN_SYMBOL_SIGNAL,
			t1, name_clk, name_clk, &clk_symbol, NULL) == PIGEN_DECLARE_OK);
		clk = pigen_signal_add(&sem_t, (pigen_syntax_id){2}, module,
			clk_symbol, t1, pigen_semantic_scalar_shape(&sem_t),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
			PIGEN_SEMANTIC_INPUT, name_clk);
		REQUIRE(clk.index != PIGEN_INVALID_ID);
		REQUIRE(pigen_symbol_declare(&sem_t, module_scope, PIGEN_SYMBOL_SIGNAL,
			t1, name_d, name_d, &d_symbol, NULL) == PIGEN_DECLARE_OK);
		d = pigen_signal_add(&sem_t, (pigen_syntax_id){3}, module,
			d_symbol, t1, pigen_semantic_scalar_shape(&sem_t),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
			PIGEN_SEMANTIC_INTERNAL, name_d);
		REQUIRE(d.index != PIGEN_INVALID_ID);
		/* (0) A second data signal for the second transfer's distinct
		 * destination payload: same data type and shape, a later arena
		 * index than d. */
		REQUIRE(pigen_symbol_declare(&sem_t, module_scope, PIGEN_SYMBOL_SIGNAL,
			t1, name_e, name_e, &e_symbol, NULL) == PIGEN_DECLARE_OK);
		e = pigen_signal_add(&sem_t, (pigen_syntax_id){7}, module,
			e_symbol, t1, pigen_semantic_scalar_shape(&sem_t),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
			PIGEN_SEMANTIC_INTERNAL, name_e);
		REQUIRE(e.index != PIGEN_INVALID_ID);

		/* The owner-span family: the clock domain interns the declared clock
		 * symbol and edge; the process carries the clock expression (a
		 * PIGEN_EXPR_SYMBOL over the clock signal whose span the process span
		 * contains) and a process span; the transfer carries a destination
		 * lvalue (the data signal's projection) and a value of the SAME data
		 * type and shape, and its span is contained in the process span. A
		 * one-atom true guard is seeded directly in the predicate arena, the
		 * way the semantic tests seed the model's predicate records. */
		domain = pigen_clock_domain_intern(&sem_t, clk_symbol,
			PIGEN_SEMANTIC_POSEDGE);
		REQUIRE(!IS_INVALID_ID(domain));
		clk_expr = pigen_expr_add_symbol(&sem_t, clk_symbol, t1, name_clk);
		REQUIRE(!IS_INVALID_ID(clk_expr));
		process = pigen_process_add(&sem_t, (pigen_syntax_id){4}, module,
			domain, clk_expr, process_span);
		REQUIRE(!IS_INVALID_ID(process));
		dest = pigen_lvalue_resolve(&sem_t,
			pigen_expr_add_symbol(&sem_t, d_symbol, t1, name_d));
		REQUIRE(!IS_INVALID_ID(dest));
		value = pigen_expr_add_integer(&sem_t, 1, t1, name_d);
		REQUIRE(!IS_INVALID_ID(value));
		sem_t.predicates = malloc(sizeof(*sem_t.predicates));
		REQUIRE(sem_t.predicates);
		sem_t.predicates[0] = (pigen_predicate){0};
		sem_t.predicate_count = 1;
		sem_t.predicate_capacity = 1;
		transfer = pigen_transfer_add(&sem_t, (pigen_syntax_id){5}, module,
			process, dest, value, (pigen_predicate_id){0}, domain,
			NULL, 0, transfer_span);
		/* The witness is valid BEFORE any stub call: the owner accepted the
		 * process and the transfer with their spans. */
		REQUIRE(!IS_INVALID_ID(transfer));
		owner_process = pigen_process_get(&sem_t, process);
		REQUIRE(owner_process &&
			owner_process->module.index == module.index &&
			owner_process->domain.index == domain.index);
		owner_transfer = pigen_transfer_get(&sem_t, transfer);
		REQUIRE(owner_transfer &&
			owner_transfer->module.index == module.index &&
			owner_transfer->process.index == process.index &&
			owner_transfer->guard.index == 0);

		/* (0) Extend the base witness for the enumeration-accessor shape:
		 * a second process (same module, same domain, a process span that
		 * contains the clock expression's span) and a second transfer on the
		 * BASE process whose destination is the distinct payload signal e
		 * (equal data type and shape, a transfer span contained in the base
		 * process span), added AFTER the first so the arena (addition) order
		 * is [first, second]. A one-atom true guard is reused from the
		 * predicate arena. A separate valid module with ZERO processes pins
		 * the empty-owner clause. All green against the landed owners. */
		process2 = pigen_process_add(&sem_t, (pigen_syntax_id){6}, module,
			domain, clk_expr, process_span2);
		REQUIRE(!IS_INVALID_ID(process2));
		dest2 = pigen_lvalue_resolve(&sem_t,
			pigen_expr_add_symbol(&sem_t, e_symbol, t1, name_e));
		REQUIRE(!IS_INVALID_ID(dest2));
		value2 = pigen_expr_add_integer(&sem_t, 1, t1, name_e);
		REQUIRE(!IS_INVALID_ID(value2));
		transfer2 = pigen_transfer_add(&sem_t, (pigen_syntax_id){8}, module,
			process, dest2, value2, (pigen_predicate_id){0}, domain,
			NULL, 0, transfer_span2);
		REQUIRE(!IS_INVALID_ID(transfer2));

		/* (0) The empty-owner module: a declared module with a clock signal
		 * but no process - a valid owner that enumerates to NULL/0. */
		{
			const char *text_empty = "module empty : input clk : logic ;\n";
			pigen_source_id source_empty;
			pigen_source_span whole_empty;
			pigen_source_span name_clk_empty;
			pigen_scope_id scope_empty;
			pigen_symbol_id module_symbol_empty;
			pigen_symbol_id clk_symbol_empty;

			source_empty = pigen_source_add(&sources_t, "lower_empty.pigen",
				text_empty, strlen(text_empty));
			REQUIRE(source_empty.index != PIGEN_INVALID_ID);
			whole_empty = (pigen_source_span){source_empty, 0,
				strlen(text_empty)};
			name_clk_empty = (pigen_source_span){source_empty, 19, 22};
			REQUIRE(pigen_symbol_declare(&sem_t, sem_t.compilation_scope,
				PIGEN_SYMBOL_MODULE,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				whole_empty, whole_empty, &module_symbol_empty, NULL) ==
				PIGEN_DECLARE_OK);
			scope_empty = pigen_scope_add(&sem_t, sem_t.compilation_scope,
				whole_empty);
			REQUIRE(scope_empty.index != PIGEN_INVALID_ID);
			module_empty = pigen_module_add(&sem_t, (pigen_syntax_id){9},
				module_symbol_empty, scope_empty, whole_empty);
			REQUIRE(module_empty.index != PIGEN_INVALID_ID);
			REQUIRE(pigen_symbol_declare(&sem_t, scope_empty,
				PIGEN_SYMBOL_SIGNAL, t1, name_clk_empty, name_clk_empty,
				&clk_symbol_empty, NULL) == PIGEN_DECLARE_OK);
		}

		/* (0) ACCESSOR SHAPE COVERAGE (green): the module enumerates exactly
		 * its two processes in arena (addition) order [first, second] - not
		 * first-match-only, not reverse order, no off-by-one count - and the
		 * base process enumerates exactly its two transfers in arena order
		 * [first, second]. The empty-owner clause returns NULL with the
		 * count left at zero, and the invalid-owner clause (id 99) does
		 * likewise. A first-match-only, reverse-order, or off-by-one-count
		 * accessor fails these. */
		module_process_count = 0;
		module_processes = pigen_module_processes(&sem_t, module,
			&module_process_count);
		REQUIRE(module_processes && module_process_count == 2 &&
			module_processes[0].index == process.index &&
			module_processes[1].index == process2.index);
		process_transfer_count = 0;
		process_transfers = pigen_process_transfers(&sem_t, process,
			&process_transfer_count);
		REQUIRE(process_transfers && process_transfer_count == 2 &&
			process_transfers[0].index == transfer.index &&
			process_transfers[1].index == transfer2.index);
		REQUIRE(pigen_module_processes(&sem_t, module_empty,
			&module_process_count) == NULL && module_process_count == 0);
		REQUIRE(pigen_module_processes(&sem_t, (pigen_module_id){99},
			&module_process_count) == NULL && module_process_count == 0);
		REQUIRE(pigen_process_transfers(&sem_t, (pigen_process_id){99},
			&process_transfer_count) == NULL && process_transfer_count == 0);

		/* The FIRST DELIBERATE RED: the valid owner witness now lowers, so
		 * the entry point must report 0. The unimplemented entry point
		 * returns -1 today, so this assert fails - at exactly this point,
		 * after the preserved green asserts above. Every fire-identity gate
		 * family below is staged RED behind this first red and only needs to
		 * compile until the lowering lands; nothing asserts that a failed
		 * call left the model or the memo maps unchanged. */
		REQUIRE(pigen_lower_rtl_transfers(&lowering_t, module) == 0);

		/* (1) FIRE IDENTITY (RED): the base single transfer's fire identity
		 * is ONE shared conjunction - the canonical guard atoms conjoined
		 * with the distinct consumer-valid and producer-ready dependencies -
		 * and every destination update of the transfer AND the source-ready
		 * equation carry that SAME lowered RTL expression identity (one
		 * memoized expression id), not a re-lowered copy. The witness is the
		 * base transfer (one-atom true guard, dest d projection): its
		 * destination update value tree and its source-ready equation value
		 * tree must share a non-constant fire-identity node. */
		{
			const pigen_semantic_transfer *base_t =
				pigen_transfer_get(&sem_t, transfer);
			pigen_rtl_expr_id upd_value;
			pigen_rtl_expr_id ready_value;
			int base_shared;

			REQUIRE(base_t);
			/* The transfer owns exactly one destination update and one
			 * source-ready equation: the update writes the projected payload,
			 * the equation drives the source ready control. */
			upd_value = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			ready_value = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			for (i = 0; i < rtl_t.update_count; i++) {
				upd = pigen_rtl_update_get(&rtl_t,
					(pigen_rtl_update_id){(uint32_t)i});
				if (upd &&
					upd->destination.index ==
						lowering_t.lowered_endpoints[d.index]
							.payload.index) {
					upd_value = upd->value;
					break;
				}
			}
			for (j = 0; j < rtl_t.equation_count; j++) {
				eq = pigen_rtl_equation_get(&rtl_t,
					(pigen_rtl_equation_id){(uint32_t)j});
				if (eq &&
					eq->destination.index ==
						lowering_t.lowered_endpoints[d.index]
							.input_ready.index) {
					ready_value = eq->value;
					break;
				}
			}
			REQUIRE(!IS_INVALID_ID(upd_value));
			base_shared = rtest_shares_nonconst(&rtl_t, upd_value,
				ready_value);
			/* The fire identity is shared by the update and the ready
			 * equation as one memoized non-constant node: a re-lowered copy
			 * (two structurally-identical but distinct ids) leaves no shared
			 * non-constant node and fails this. */
			REQUIRE(base_shared);
		}

		/* (2) JOIN (RED): two transfers in one module sharing ONE destination
		 * fire on ONE shared fire identity - the same lowered expression
		 * identity on both. The witness is a module with two transfers whose
		 * destination lvalue is the same payload signal; both destination
		 * updates must carry the same non-constant fire-identity node. */
		{
			const char *text_join =
				"module join : input clk : logic ; input d : logic ;\n";
			pigen_source_manager sources_a = {0};
			pigen_semantic_model sem_a;
			pigen_rtl_model rtl_a = {0};
			pigen_rtl_lowering lowering_a;
			pigen_data_type_id ta;
			pigen_source_id source_a;
			pigen_source_span whole_a, name_clk_a, name_d_a;
			pigen_source_span proc_a, tr_a_span;
			pigen_scope_id scope_a;
			pigen_symbol_id mod_a, clk_a, d_a;
			pigen_module_id module_a;
			pigen_signal_id clk_sig_a, d_sig_a;
			pigen_clock_domain_id dom_a;
			pigen_expr_id clk_e_a;
			pigen_process_id proc_id_a;
			pigen_lvalue_id dest_a;
			pigen_expr_id val_a;
			pigen_transfer_id tr_x, tr_y;
			pigen_rtl_expr_id x_val, y_val;
			int join_shared;

			pigen_semantic_init(&sem_a, &sources_a);
			pigen_rtl_lowering_init(&lowering_a, &sem_a, &rtl_a);
			ta = pigen_data_type_sized_logic(&sem_a, 1, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(ta));
			source_a = pigen_source_add(&sources_a, "lower_join.pigen",
				text_join, strlen(text_join));
			REQUIRE(source_a.index != PIGEN_INVALID_ID);
			whole_a = (pigen_source_span){source_a, 0, strlen(text_join)};
			name_clk_a = (pigen_source_span){source_a, 20, 23};
			name_d_a = (pigen_source_span){source_a, 40, 41};
			proc_a = (pigen_source_span){source_a, 13, 41};
			tr_a_span = (pigen_source_span){source_a, 33, 41};
			sem_a.compilation_scope = pigen_scope_add(&sem_a,
				(pigen_scope_id){PIGEN_INVALID_ID},
				(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
			REQUIRE(sem_a.compilation_scope.index != PIGEN_INVALID_ID);
			REQUIRE(pigen_symbol_declare(&sem_a, sem_a.compilation_scope,
				PIGEN_SYMBOL_MODULE,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				whole_a, whole_a, &mod_a, NULL) == PIGEN_DECLARE_OK);
			scope_a = pigen_scope_add(&sem_a, sem_a.compilation_scope, whole_a);
			module_a = pigen_module_add(&sem_a, (pigen_syntax_id){1}, mod_a,
				scope_a, whole_a);
			REQUIRE(module_a.index != PIGEN_INVALID_ID);
			REQUIRE(pigen_symbol_declare(&sem_a, scope_a, PIGEN_SYMBOL_SIGNAL,
				ta, name_clk_a, name_clk_a, &clk_a, NULL) ==
				PIGEN_DECLARE_OK);
			clk_sig_a = pigen_signal_add(&sem_a, (pigen_syntax_id){2},
				module_a, clk_a, ta, pigen_semantic_scalar_shape(&sem_a),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INPUT, name_clk_a);
			(void)clk_sig_a;
			REQUIRE(pigen_symbol_declare(&sem_a, scope_a, PIGEN_SYMBOL_SIGNAL,
				ta, name_d_a, name_d_a, &d_a, NULL) == PIGEN_DECLARE_OK);
			d_sig_a = pigen_signal_add(&sem_a, (pigen_syntax_id){3},
				module_a, d_a, ta, pigen_semantic_scalar_shape(&sem_a),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INTERNAL, name_d_a);
			dom_a = pigen_clock_domain_intern(&sem_a, clk_a,
				PIGEN_SEMANTIC_POSEDGE);
			clk_e_a = pigen_expr_add_symbol(&sem_a, clk_a, ta, name_clk_a);
			proc_id_a = pigen_process_add(&sem_a, (pigen_syntax_id){4},
				module_a, dom_a, clk_e_a, proc_a);
			dest_a = pigen_lvalue_resolve(&sem_a,
				pigen_expr_add_symbol(&sem_a, d_a, ta, name_d_a));
			val_a = pigen_expr_add_integer(&sem_a, 1, ta, name_d_a);
			sem_a.predicates = malloc(sizeof(*sem_a.predicates));
			sem_a.predicates[0] = (pigen_predicate){0};
			sem_a.predicate_count = 1;
			sem_a.predicate_capacity = 1;
			tr_x = pigen_transfer_add(&sem_a, (pigen_syntax_id){5}, module_a,
				proc_id_a, dest_a, val_a, (pigen_predicate_id){0}, dom_a,
				NULL, 0, tr_a_span);
			tr_y = pigen_transfer_add(&sem_a, (pigen_syntax_id){6}, module_a,
				proc_id_a, dest_a, val_a, (pigen_predicate_id){0}, dom_a,
				NULL, 0, tr_a_span);
			REQUIRE(!IS_INVALID_ID(tr_x) && !IS_INVALID_ID(tr_y));
			REQUIRE(pigen_lower_rtl_transfers(&lowering_a, module_a) == 0);
			/* Both transfers' destination updates (same payload object) must
			 * carry the SAME non-constant fire-identity node: the join fires
			 * on one shared identity, not two re-lowered copies. */
			x_val = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			y_val = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			for (i = 0; i < rtl_a.update_count; i++) {
				upd = pigen_rtl_update_get(&rtl_a,
					(pigen_rtl_update_id){(uint32_t)i});
				if (upd &&
					upd->destination.index ==
						lowering_a.lowered_endpoints[d_sig_a.index]
							.payload.index) {
					if (IS_INVALID_ID(x_val))
						x_val = upd->value;
					else
						y_val = upd->value;
				}
			}
			REQUIRE(!IS_INVALID_ID(x_val) && !IS_INVALID_ID(y_val));
			join_shared = rtest_shares_nonconst(&rtl_a, x_val, y_val);
			REQUIRE(join_shared);
			pigen_rtl_lowering_free(&lowering_a);
			pigen_free_rtl_model(&rtl_a);
			pigen_free_semantic_model(&sem_a);
			pigen_free_sources(&sources_a);
		}

		/* (3) REPEATED PROJECTION (RED): two transfers projecting the SAME
		 * payload signal lower to ONE shared projection object - no duplicate
		 * RTL object. The witness is a module with two transfers whose dest
		 * lvalue resolves to the same base signal; the declaration lowering
		 * must publish exactly one payload object for that signal, shared by
		 * both transfers' destination updates. */
		{
			const char *text_proj =
				"module proj : input clk : logic ; input d : logic ;\n";
			pigen_source_manager sources_b = {0};
			pigen_semantic_model sem_b;
			pigen_rtl_model rtl_b = {0};
			pigen_rtl_lowering lowering_b;
			pigen_data_type_id tb;
			pigen_source_id source_b;
			pigen_source_span whole_b, name_clk_b, name_d_b;
			pigen_source_span proc_b, tr_b_span;
			pigen_scope_id scope_b;
			pigen_symbol_id mod_b, clk_b, d_b;
			pigen_module_id module_b;
			pigen_signal_id clk_sig_b, d_sig_b;
			pigen_clock_domain_id dom_b;
			pigen_expr_id clk_e_b;
			pigen_process_id proc_id_b;
			pigen_lvalue_id dest_b;
			pigen_expr_id val_b;
			pigen_transfer_id tr_p, tr_q;
			size_t object_refs;

			pigen_semantic_init(&sem_b, &sources_b);
			pigen_rtl_lowering_init(&lowering_b, &sem_b, &rtl_b);
			tb = pigen_data_type_sized_logic(&sem_b, 1, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(tb));
			source_b = pigen_source_add(&sources_b, "lower_proj.pigen",
				text_proj, strlen(text_proj));
			REQUIRE(source_b.index != PIGEN_INVALID_ID);
			whole_b = (pigen_source_span){source_b, 0, strlen(text_proj)};
			name_clk_b = (pigen_source_span){source_b, 20, 23};
			name_d_b = (pigen_source_span){source_b, 40, 41};
			proc_b = (pigen_source_span){source_b, 13, 41};
			tr_b_span = (pigen_source_span){source_b, 33, 41};
			sem_b.compilation_scope = pigen_scope_add(&sem_b,
				(pigen_scope_id){PIGEN_INVALID_ID},
				(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
			REQUIRE(pigen_symbol_declare(&sem_b, sem_b.compilation_scope,
				PIGEN_SYMBOL_MODULE,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				whole_b, whole_b, &mod_b, NULL) == PIGEN_DECLARE_OK);
			scope_b = pigen_scope_add(&sem_b, sem_b.compilation_scope, whole_b);
			module_b = pigen_module_add(&sem_b, (pigen_syntax_id){1}, mod_b,
				scope_b, whole_b);
			REQUIRE(pigen_symbol_declare(&sem_b, scope_b, PIGEN_SYMBOL_SIGNAL,
				tb, name_clk_b, name_clk_b, &clk_b, NULL) ==
				PIGEN_DECLARE_OK);
			clk_sig_b = pigen_signal_add(&sem_b, (pigen_syntax_id){2},
				module_b, clk_b, tb, pigen_semantic_scalar_shape(&sem_b),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INPUT, name_clk_b);
			(void)clk_sig_b;
			REQUIRE(pigen_symbol_declare(&sem_b, scope_b, PIGEN_SYMBOL_SIGNAL,
				tb, name_d_b, name_d_b, &d_b, NULL) == PIGEN_DECLARE_OK);
			d_sig_b = pigen_signal_add(&sem_b, (pigen_syntax_id){3},
				module_b, d_b, tb, pigen_semantic_scalar_shape(&sem_b),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INTERNAL, name_d_b);
			dom_b = pigen_clock_domain_intern(&sem_b, clk_b,
				PIGEN_SEMANTIC_POSEDGE);
			clk_e_b = pigen_expr_add_symbol(&sem_b, clk_b, tb, name_clk_b);
			proc_id_b = pigen_process_add(&sem_b, (pigen_syntax_id){4},
				module_b, dom_b, clk_e_b, proc_b);
			dest_b = pigen_lvalue_resolve(&sem_b,
				pigen_expr_add_symbol(&sem_b, d_b, tb, name_d_b));
			val_b = pigen_expr_add_integer(&sem_b, 1, tb, name_d_b);
			sem_b.predicates = malloc(sizeof(*sem_b.predicates));
			sem_b.predicates[0] = (pigen_predicate){0};
			sem_b.predicate_count = 1;
			sem_b.predicate_capacity = 1;
			tr_p = pigen_transfer_add(&sem_b, (pigen_syntax_id){5}, module_b,
				proc_id_b, dest_b, val_b, (pigen_predicate_id){0}, dom_b,
				NULL, 0, tr_b_span);
			tr_q = pigen_transfer_add(&sem_b, (pigen_syntax_id){6}, module_b,
				proc_id_b, dest_b, val_b, (pigen_predicate_id){0}, dom_b,
				NULL, 0, tr_b_span);
			REQUIRE(!IS_INVALID_ID(tr_p) && !IS_INVALID_ID(tr_q));
			REQUIRE(pigen_lower_rtl_transfers(&lowering_b, module_b) == 0);
			/* The two transfers share ONE payload object for d: exactly one
			 * RTL object records d as its semantic signal, and both
			 * destination updates target it. A duplicate projection object
			 * (two objects for the same signal) fails this. */
			object_refs = 0;
			for (i = 0; i < rtl_b.object_count; i++) {
				const pigen_rtl_object *obj = pigen_rtl_object_get(&rtl_b,
					(pigen_rtl_object_id){(uint32_t)i});
				if (obj && obj->semantic_signal.index == d_sig_b.index)
					object_refs++;
			}
			REQUIRE(object_refs == 1);
			pigen_rtl_lowering_free(&lowering_b);
			pigen_free_rtl_model(&rtl_b);
			pigen_free_semantic_model(&sem_b);
			pigen_free_sources(&sources_b);
		}

		/* (4) STATIC-ONLY ASSIGNMENT (RED): a static lvalue destination
		 * lowers with no clock-domain update beyond its fire identity - the
		 * destination update carries the fire identity and the static payload
		 * value, and there is no separate clocked storage update for the
		 * static. The witness is a transfer whose dest is the static (REG)
		 * payload signal; its single destination update must reference the
		 * fire-identity node and the static payload object. */
		{
			const char *text_static =
				"module stat : input clk : logic ; reg d : logic ;\n";
			pigen_source_manager sources_c = {0};
			pigen_semantic_model sem_c;
			pigen_rtl_model rtl_c = {0};
			pigen_rtl_lowering lowering_c;
			pigen_data_type_id tc;
			pigen_source_id source_c;
			pigen_source_span whole_c, name_clk_c, name_d_c;
			pigen_source_span proc_c, tr_c_span;
			pigen_scope_id scope_c;
			pigen_symbol_id mod_c, clk_c, d_c;
			pigen_module_id module_c;
			pigen_signal_id clk_sig_c, d_sig_c;
			pigen_clock_domain_id dom_c;
			pigen_expr_id clk_e_c;
			pigen_process_id proc_id_c;
			pigen_lvalue_id dest_c;
			pigen_expr_id val_c;
			pigen_transfer_id tr_s;
			int s_static;

			pigen_semantic_init(&sem_c, &sources_c);
			pigen_rtl_lowering_init(&lowering_c, &sem_c, &rtl_c);
			tc = pigen_data_type_sized_logic(&sem_c, 1, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(tc));
			source_c = pigen_source_add(&sources_c, "lower_static.pigen",
				text_static, strlen(text_static));
			REQUIRE(source_c.index != PIGEN_INVALID_ID);
			whole_c = (pigen_source_span){source_c, 0, strlen(text_static)};
			name_clk_c = (pigen_source_span){source_c, 20, 23};
			name_d_c = (pigen_source_span){source_c, 38, 39};
			proc_c = (pigen_source_span){source_c, 13, 45};
			tr_c_span = (pigen_source_span){source_c, 34, 40};
			sem_c.compilation_scope = pigen_scope_add(&sem_c,
				(pigen_scope_id){PIGEN_INVALID_ID},
				(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
			REQUIRE(pigen_symbol_declare(&sem_c, sem_c.compilation_scope,
				PIGEN_SYMBOL_MODULE,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				whole_c, whole_c, &mod_c, NULL) == PIGEN_DECLARE_OK);
			scope_c = pigen_scope_add(&sem_c, sem_c.compilation_scope, whole_c);
			module_c = pigen_module_add(&sem_c, (pigen_syntax_id){1}, mod_c,
				scope_c, whole_c);
			REQUIRE(pigen_symbol_declare(&sem_c, scope_c, PIGEN_SYMBOL_SIGNAL,
				tc, name_clk_c, name_clk_c, &clk_c, NULL) ==
				PIGEN_DECLARE_OK);
			clk_sig_c = pigen_signal_add(&sem_c, (pigen_syntax_id){2},
				module_c, clk_c, tc, pigen_semantic_scalar_shape(&sem_c),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INPUT, name_clk_c);
			(void)clk_sig_c;
			REQUIRE(pigen_symbol_declare(&sem_c, scope_c, PIGEN_SYMBOL_SIGNAL,
				tc, name_d_c, name_d_c, &d_c, NULL) == PIGEN_DECLARE_OK);
			d_sig_c = pigen_signal_add(&sem_c, (pigen_syntax_id){3},
				module_c, d_c, tc, pigen_semantic_scalar_shape(&sem_c),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_REG,
				PIGEN_SEMANTIC_INTERNAL, name_d_c);
			dom_c = pigen_clock_domain_intern(&sem_c, clk_c,
				PIGEN_SEMANTIC_POSEDGE);
			clk_e_c = pigen_expr_add_symbol(&sem_c, clk_c, tc, name_clk_c);
			proc_id_c = pigen_process_add(&sem_c, (pigen_syntax_id){4},
				module_c, dom_c, clk_e_c, proc_c);
			dest_c = pigen_lvalue_resolve(&sem_c,
				pigen_expr_add_symbol(&sem_c, d_c, tc, name_d_c));
			val_c = pigen_expr_add_integer(&sem_c, 1, tc, name_d_c);
			sem_c.predicates = malloc(sizeof(*sem_c.predicates));
			sem_c.predicates[0] = (pigen_predicate){0};
			sem_c.predicate_count = 1;
			sem_c.predicate_capacity = 1;
			tr_s = pigen_transfer_add(&sem_c, (pigen_syntax_id){5}, module_c,
				proc_id_c, dest_c, val_c, (pigen_predicate_id){0}, dom_c,
				NULL, 0, tr_c_span);
			REQUIRE(!IS_INVALID_ID(tr_s));
			REQUIRE(pigen_lower_rtl_transfers(&lowering_c, module_c) == 0);
			/* The static destination lowers to ONE clocked update (the fire
			 * identity applied to the static payload), not a separate
			 * storage update: exactly one update targets the static payload
			 * object - not zero, not two. */
			{
				size_t static_updates = 0;
				for (i = 0; i < rtl_c.update_count; i++) {
					upd = pigen_rtl_update_get(&rtl_c,
						(pigen_rtl_update_id){(uint32_t)i});
					if (upd &&
						upd->destination.index ==
							lowering_c.lowered_endpoints[d_sig_c.index]
								.payload.index)
						static_updates++;
				}
				s_static = (static_updates == 1);
			}
			REQUIRE(s_static);
			pigen_rtl_lowering_free(&lowering_c);
			pigen_free_rtl_model(&rtl_c);
			pigen_free_semantic_model(&sem_c);
			pigen_free_sources(&sources_c);
		}

		/* (5) PROCESS ORDER (RED): two processes in one module each lower
		 * their transfers into their OWN RTL process, in arena (addition)
		 * order, with no cross-process equation sharing. The witness is a
		 * module with two processes, one transfer each; the RTL module's
		 * processes range must hold two processes in arena order, each
		 * owning its own updates, and no equation or update is shared across
		 * the two processes. */
		{
			const char *text_proc =
				"module proc : input clk : logic ; input d : logic ; input e : logic ;\n";
			pigen_source_manager sources_d = {0};
			pigen_semantic_model sem_d;
			pigen_rtl_model rtl_d = {0};
			pigen_rtl_lowering lowering_d;
			pigen_data_type_id td;
			pigen_source_id source_d;
			pigen_source_span whole_d, name_clk_d, name_d_d, name_e_d;
			pigen_source_span proc1_d, proc2_d;
			pigen_source_span tr1_d, tr2_d;
			pigen_scope_id scope_d;
			pigen_symbol_id mod_d, clk_d, d_d, e_d;
			pigen_module_id module_d;
			pigen_signal_id clk_sig_d, d_sig_d, e_sig_d;
			pigen_clock_domain_id dom_d;
			pigen_expr_id clk_e_d;
			pigen_process_id proc1_id, proc2_id;
			pigen_lvalue_id dest1, dest2;
			pigen_expr_id val1, val2;
			pigen_transfer_id tr1, tr2;
			const pigen_rtl_update *upd1_d, *upd2_d;
			int d_order;

			pigen_semantic_init(&sem_d, &sources_d);
			pigen_rtl_lowering_init(&lowering_d, &sem_d, &rtl_d);
			td = pigen_data_type_sized_logic(&sem_d, 1, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(td));
			source_d = pigen_source_add(&sources_d, "lower_proc.pigen",
				text_proc, strlen(text_proc));
			REQUIRE(source_d.index != PIGEN_INVALID_ID);
			whole_d = (pigen_source_span){source_d, 0, strlen(text_proc)};
			name_clk_d = (pigen_source_span){source_d, 20, 23};
			name_d_d = (pigen_source_span){source_d, 40, 41};
			name_e_d = (pigen_source_span){source_d, 58, 59};
			proc1_d = (pigen_source_span){source_d, 13, 40};
			proc2_d = (pigen_source_span){source_d, 13, 60};
			tr1_d = (pigen_source_span){source_d, 33, 40};
			tr2_d = (pigen_source_span){source_d, 53, 60};
			sem_d.compilation_scope = pigen_scope_add(&sem_d,
				(pigen_scope_id){PIGEN_INVALID_ID},
				(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
			REQUIRE(pigen_symbol_declare(&sem_d, sem_d.compilation_scope,
				PIGEN_SYMBOL_MODULE,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				whole_d, whole_d, &mod_d, NULL) == PIGEN_DECLARE_OK);
			scope_d = pigen_scope_add(&sem_d, sem_d.compilation_scope, whole_d);
			module_d = pigen_module_add(&sem_d, (pigen_syntax_id){1}, mod_d,
				scope_d, whole_d);
			REQUIRE(pigen_symbol_declare(&sem_d, scope_d, PIGEN_SYMBOL_SIGNAL,
				td, name_clk_d, name_clk_d, &clk_d, NULL) ==
				PIGEN_DECLARE_OK);
			clk_sig_d = pigen_signal_add(&sem_d, (pigen_syntax_id){2},
				module_d, clk_d, td, pigen_semantic_scalar_shape(&sem_d),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INPUT, name_clk_d);
			(void)clk_sig_d;
			REQUIRE(pigen_symbol_declare(&sem_d, scope_d, PIGEN_SYMBOL_SIGNAL,
				td, name_d_d, name_d_d, &d_d, NULL) == PIGEN_DECLARE_OK);
			d_sig_d = pigen_signal_add(&sem_d, (pigen_syntax_id){3},
				module_d, d_d, td, pigen_semantic_scalar_shape(&sem_d),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INTERNAL, name_d_d);
			(void)d_sig_d;
			REQUIRE(pigen_symbol_declare(&sem_d, scope_d, PIGEN_SYMBOL_SIGNAL,
				td, name_e_d, name_e_d, &e_d, NULL) == PIGEN_DECLARE_OK);
			e_sig_d = pigen_signal_add(&sem_d, (pigen_syntax_id){4},
				module_d, e_d, td, pigen_semantic_scalar_shape(&sem_d),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INTERNAL, name_e_d);
			(void)e_sig_d;
			dom_d = pigen_clock_domain_intern(&sem_d, clk_d,
				PIGEN_SEMANTIC_POSEDGE);
			clk_e_d = pigen_expr_add_symbol(&sem_d, clk_d, td, name_clk_d);
			proc1_id = pigen_process_add(&sem_d, (pigen_syntax_id){5},
				module_d, dom_d, clk_e_d, proc1_d);
			proc2_id = pigen_process_add(&sem_d, (pigen_syntax_id){6},
				module_d, dom_d, clk_e_d, proc2_d);
			dest1 = pigen_lvalue_resolve(&sem_d,
				pigen_expr_add_symbol(&sem_d, d_d, td, name_d_d));
			val1 = pigen_expr_add_integer(&sem_d, 1, td, name_d_d);
			dest2 = pigen_lvalue_resolve(&sem_d,
				pigen_expr_add_symbol(&sem_d, e_d, td, name_e_d));
			val2 = pigen_expr_add_integer(&sem_d, 1, td, name_e_d);
			sem_d.predicates = malloc(sizeof(*sem_d.predicates));
			sem_d.predicates[0] = (pigen_predicate){0};
			sem_d.predicate_count = 1;
			sem_d.predicate_capacity = 1;
			tr1 = pigen_transfer_add(&sem_d, (pigen_syntax_id){7}, module_d,
				proc1_id, dest1, val1, (pigen_predicate_id){0}, dom_d,
				NULL, 0, tr1_d);
			tr2 = pigen_transfer_add(&sem_d, (pigen_syntax_id){8}, module_d,
				proc2_id, dest2, val2, (pigen_predicate_id){0}, dom_d,
				NULL, 0, tr2_d);
			REQUIRE(!IS_INVALID_ID(tr1) && !IS_INVALID_ID(tr2));
			REQUIRE(pigen_lower_rtl_transfers(&lowering_d, module_d) == 0);
			/* Two semantic processes lower to two RTL processes in arena
			 * (addition) order, each owning exactly its own updates on its
			 * own destination payload: process1's single update (dest d)
			 * and process2's single update (dest e), nothing shared. A
			 * merged/single RTL process, reordered processes or a
			 * cross-process update fails this. */
			d_order = (rtl_d.process_count == 2);
			if (d_order) {
				rtlp = pigen_rtl_process_get(&rtl_d,
					(pigen_rtl_process_id){0});
				if (!rtlp || rtlp->updates.count != 1)
					d_order = 0;
				upd1_d = (rtlp && rtlp->updates.count == 1) ?
					pigen_rtl_update_get(&rtl_d,
						(pigen_rtl_update_id){rtlp->updates.first}) :
					NULL;
				d_order = d_order && upd1_d &&
					upd1_d->destination.index ==
						lowering_d.lowered_endpoints[d_sig_d.index]
							.payload.index;
			}
			if (d_order) {
				rtlp = pigen_rtl_process_get(&rtl_d,
					(pigen_rtl_process_id){1});
				if (!rtlp || rtlp->updates.count != 1)
					d_order = 0;
				upd2_d = (rtlp && rtlp->updates.count == 1) ?
					pigen_rtl_update_get(&rtl_d,
						(pigen_rtl_update_id){rtlp->updates.first}) :
					NULL;
				d_order = d_order && upd2_d &&
					upd2_d->destination.index ==
						lowering_d.lowered_endpoints[e_sig_d.index]
							.payload.index;
			}
			REQUIRE(d_order);
			pigen_rtl_lowering_free(&lowering_d);
			pigen_free_rtl_model(&rtl_d);
			pigen_free_semantic_model(&sem_d);
			pigen_free_sources(&sources_d);
		}

		/* (6) MODULE COMPOSITION (RED): after the base witness's transfers
		 * have lowered, composing the module publishes its module record - the
		 * entry point returns a valid module identity (the invalid return is
		 * the owner's rejection path only) and the published record's
		 * object/instance/equation/process ranges cover exactly the records
		 * published for the witness, in arena order: the destination payload
		 * objects through their declaration endpoints, the source-ready
		 * equations on the input-ready endpoints, and one RTL process per
		 * semantic process with its own updates. The witness is the base
		 * witness module: two processes (the base process owns both
		 * transfers, the second owns none). Per how/should/compiler/builders/
		 * fail.md the failure path may leave partial state behind; nothing
		 * below asserts that a failed composition left the model or the maps
		 * unchanged. */
		{
			pigen_rtl_module_id composed;
			const pigen_rtl_module *composed_get;
			const pigen_rtl_object *comp_obj;
			const pigen_rtl_instance *comp_inst;
			const pigen_rtl_equation *comp_eq;
			const pigen_rtl_process *comp_proc;
			const pigen_process_id *comp_sem_processes;
			const pigen_transfer_id *comp_sem_transfers;
			size_t comp_sem_process_count;
			size_t comp_sem_transfer_count;
			int m_objects, m_instances, m_equations, m_processes;

			composed = pigen_lower_rtl_module(&lowering_t, module);
			REQUIRE(!IS_INVALID_ID(composed));
			composed_get = pigen_rtl_module_get(&rtl_t, composed);
			REQUIRE(composed_get);
			/* OBJECTS: the composed range covers exactly the objects published
			 * for the witness module - no record escapes the range and no
			 * foreign record enters it. Every destination update and
			 * source-ready equation targets its signal's published payload
			 * object, so the endpoint payloads d and e must both sit in the
			 * range, and every object in the range is this module's. */
			m_objects = 1;
			for (i = composed_get->objects.first; i < composed_get->objects.first +
				composed_get->objects.count && m_objects; i++) {
				comp_obj = pigen_rtl_object_get(&rtl_t,
					(pigen_rtl_object_id){(uint32_t)i});
				if (!comp_obj ||
					comp_obj->module.index != composed.index)
					m_objects = 0;
			}
			if (!IS_INVALID_ID(lowering_t.lowered_endpoints[d.index]
					.payload) &&
				!IS_INVALID_ID(lowering_t.lowered_endpoints[e.index]
					.payload)) {
				m_objects &= composed_get->objects.count >= 2 &&
					composed_get->objects.first <=
						lowering_t.lowered_endpoints[d.index]
							.payload.index &&
					lowering_t.lowered_endpoints[d.index].payload.index <
						composed_get->objects.first +
							composed_get->objects.count &&
					composed_get->objects.first <=
						lowering_t.lowered_endpoints[e.index]
							.payload.index &&
					lowering_t.lowered_endpoints[e.index].payload.index <
						composed_get->objects.first +
							composed_get->objects.count;
			}
			REQUIRE(m_objects);
			/* INSTANCES: the composed range holds only this module's
			 * instances. The witness declares no storage, so the range is
			 * empty today; a foreign or unowned instance in the range fails
			 * this. */
			m_instances = 1;
			for (i = composed_get->instances.first;
				i < composed_get->instances.first +
					composed_get->instances.count && m_instances; i++) {
				comp_inst = pigen_rtl_instance_get(&rtl_t,
					(pigen_rtl_instance_id){(uint32_t)i});
				if (!comp_inst ||
					comp_inst->module.index != composed.index)
					m_instances = 0;
			}
			REQUIRE(m_instances);
			/* EQUATIONS: the composed range covers exactly the equations
			 * published for the witness module - including the source-ready
			 * equations on the input-ready endpoints of d and e - and every
			 * equation in the range is this module's. A ready equation left
			 * out of the range (or a foreign one in it) fails this. */
			m_equations = 1;
			for (i = composed_get->equations.first;
				i < composed_get->equations.first +
					composed_get->equations.count && m_equations; i++) {
				comp_eq = pigen_rtl_equation_get(&rtl_t,
					(pigen_rtl_equation_id){(uint32_t)i});
				if (!comp_eq ||
					comp_eq->module.index != composed.index)
					m_equations = 0;
			}
			if (!IS_INVALID_ID(lowering_t.lowered_endpoints[d.index]
					.input_ready) &&
				!IS_INVALID_ID(lowering_t.lowered_endpoints[e.index]
					.input_ready)) {
				for (j = 0; j < rtl_t.equation_count && m_equations; j++) {
					const pigen_rtl_equation *ready =
						pigen_rtl_equation_get(&rtl_t,
							(pigen_rtl_equation_id){(uint32_t)j});
					if (!ready)
						continue;
					if (ready->destination.index ==
						lowering_t.lowered_endpoints[d.index]
							.input_ready.index ||
						ready->destination.index ==
						lowering_t.lowered_endpoints[e.index]
							.input_ready.index) {
						m_equations &=
							composed_get->equations.first <= j &&
							j < composed_get->equations.first +
								composed_get->equations.count;
					}
				}
			}
			REQUIRE(m_equations);
			/* PROCESSES: one RTL process per semantic process, in arena
			 * (addition) order [first, second] - the base process owns its
			 * two transfers' updates and the empty second process owns none -
			 * and every process in the range is this module's. A merged,
			 * missing or reordered RTL process fails this. */
			m_processes = 1;
			comp_sem_process_count = 0;
			comp_sem_processes = pigen_module_processes(&sem_t, module,
				&comp_sem_process_count);
			if (!comp_sem_processes ||
				comp_sem_process_count !=
					composed_get->processes.count)
				m_processes = 0;
			for (i = composed_get->processes.first;
				i < composed_get->processes.first +
					composed_get->processes.count && m_processes; i++) {
				comp_proc = pigen_rtl_process_get(&rtl_t,
					(pigen_rtl_process_id){(uint32_t)i});
				if (!comp_proc ||
					comp_proc->module.index != composed.index)
					m_processes = 0;
			}
			if (m_processes) {
				for (i = 0;
					i < composed_get->processes.count &&
					i < comp_sem_process_count && m_processes; i++) {
					const pigen_rtl_process *ordered =
						pigen_rtl_process_get(&rtl_t,
							(pigen_rtl_process_id){
								(uint32_t)(
									composed_get->processes.first +
									i)});
					if (!ordered)
						m_processes = 0;
					else if (i == 0) {
						/* The base process's RTL process owns exactly
						 * its two transfers' destination updates:
						 * one update each, in transfer arena
						 * order, on the d and e payload objects. */
						comp_sem_transfer_count = 0;
						comp_sem_transfers =
							pigen_process_transfers(
								&sem_t,
								comp_sem_processes[0],
								&comp_sem_transfer_count);
						if (!comp_sem_transfers ||
							comp_sem_transfer_count !=
								ordered->updates.count)
							m_processes = 0;
						for (j = 0;
							j < comp_sem_transfer_count &&
							m_processes; j++) {
							const pigen_semantic_transfer
								*own =
									pigen_transfer_get(
										&sem_t,
										comp_sem_transfers[j]);
							const pigen_rtl_update
								*up =
									pigen_rtl_update_get(
										&rtl_t,
										(pigen_rtl_update_id){
											(uint32_t)(
												ordered->updates
													.first +
												j)});
							pigen_rtl_object_id want;
							const pigen_semantic_lvalue *own_dest =
								pigen_lvalue_get(&sem_t,
									own->destination);
							if (!own || !up || !own_dest)
								m_processes = 0;
							else {
								want = lowering_t
									.lowered_endpoints[
										own_dest->as
											.projection
											.signal.index]
									.payload;
								m_processes &=
									!IS_INVALID_ID(want) &&
									up->destination.index ==
										want.index;
							}
						}
					} else if (i == 1) {
						/* The second semantic process owns no
						 * transfers: its RTL process publishes an
						 * empty updates range. */
						m_processes &=
							ordered->updates.count == 0;
					}
				}
			}
			REQUIRE(m_processes);
		}

		/* (7) GUARD AND DEPENDENCY FIRE IDENTITY (RED): every witness so
		 * far seeds its guard as the zero-atom predicate and passes NULL,
		 * 0 signal uses to pigen_transfer_add, so the staged fire-identity
		 * asserts pin only that the destination update and the source-ready
		 * equation share one non-constant node. This witness closes that
		 * escape: its guard is built through the owner predicate API as a
		 * two-atom conjunction (g == 1 AND f == 0 - the expected-0 atom is
		 * the negation) and its transfer carries five distinct (signal,
		 * role) uses, making the guard-atom conjunction, the endpoint
		 * valid/ready conjuncts and the (signal, role) dedup observable to
		 * the assertion suite that follows. The witness is a fresh
		 * one-process module: clock clk, destination payload d, guard
		 * conditions g and f, consumer c and producer p; its single
		 * transfer assigns the 1-bit constant 1 to d under the guard. */
		{
			const char *text_g =
				"module guard : input clk : logic ; input d : logic ; "
				"input g : logic ; input f : logic ; input c : logic ; "
				"input p : logic ;\n";
			pigen_source_manager sources_g = {0};
			pigen_semantic_model sem_g;
			pigen_rtl_model rtl_g = {0};
			pigen_rtl_lowering lowering_g;
			pigen_data_type_id td_g;
			pigen_source_id source_g;
			pigen_source_span whole_g, name_clk_g, name_d_g, name_g_g;
			pigen_source_span name_f_g, name_c_g, name_p_g, proc_g;
			pigen_source_span tr_g;
			pigen_scope_id scope_g;
			pigen_symbol_id mod_g, clk_g, d_g, g_g, f_g, c_g, p_g;
			pigen_module_id module_g;
			pigen_signal_id clk_sig_g, d_sig_g, g_sig_g, f_sig_g;
			pigen_signal_id c_sig_g, p_sig_g;
			pigen_clock_domain_id dom_g;
			pigen_expr_id clk_e_g, val_g, g_e, f_e;
			pigen_process_id proc_id_g;
			pigen_lvalue_id dest_g;
			pigen_predicate_id true_g, guard_g;
			const pigen_predicate *guard_get;
			const pigen_predicate_atom *guard_atoms;
			pigen_transfer_signal_use uses_g[5];
			pigen_transfer_id tr_id_g;
			const pigen_semantic_transfer *tr_get;

			pigen_semantic_init(&sem_g, &sources_g);
			pigen_rtl_lowering_init(&lowering_g, &sem_g, &rtl_g);
			td_g = pigen_data_type_sized_logic(&sem_g, 1, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(td_g));
			source_g = pigen_source_add(&sources_g, "lower_guard.pigen",
				text_g, strlen(text_g));
			REQUIRE(source_g.index != PIGEN_INVALID_ID);
			whole_g = (pigen_source_span){source_g, 0, strlen(text_g)};
			name_clk_g = (pigen_source_span){source_g, 21, 24};
			name_d_g = (pigen_source_span){source_g, 41, 42};
			name_g_g = (pigen_source_span){source_g, 59, 60};
			name_f_g = (pigen_source_span){source_g, 77, 78};
			name_c_g = (pigen_source_span){source_g, 95, 96};
			name_p_g = (pigen_source_span){source_g, 113, 114};
			proc_g = (pigen_source_span){source_g, 15, 124};
			tr_g = (pigen_source_span){source_g, 35, 52};
			sem_g.compilation_scope = pigen_scope_add(&sem_g,
				(pigen_scope_id){PIGEN_INVALID_ID},
				(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
			REQUIRE(pigen_symbol_declare(&sem_g, sem_g.compilation_scope,
				PIGEN_SYMBOL_MODULE,
				(pigen_data_type_id){PIGEN_INVALID_ID},
				whole_g, whole_g, &mod_g, NULL) == PIGEN_DECLARE_OK);
			scope_g = pigen_scope_add(&sem_g, sem_g.compilation_scope,
				whole_g);
			module_g = pigen_module_add(&sem_g, (pigen_syntax_id){1},
				mod_g, scope_g, whole_g);
			REQUIRE(pigen_symbol_declare(&sem_g, scope_g,
				PIGEN_SYMBOL_SIGNAL, td_g, name_clk_g, name_clk_g,
				&clk_g, NULL) == PIGEN_DECLARE_OK);
			clk_sig_g = pigen_signal_add(&sem_g, (pigen_syntax_id){2},
				module_g, clk_g, td_g, pigen_semantic_scalar_shape(&sem_g),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INPUT, name_clk_g);
			(void)clk_sig_g;
			REQUIRE(pigen_symbol_declare(&sem_g, scope_g,
				PIGEN_SYMBOL_SIGNAL, td_g, name_d_g, name_d_g,
				&d_g, NULL) == PIGEN_DECLARE_OK);
			d_sig_g = pigen_signal_add(&sem_g, (pigen_syntax_id){3},
				module_g, d_g, td_g, pigen_semantic_scalar_shape(&sem_g),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INTERNAL, name_d_g);
			(void)d_sig_g;
			REQUIRE(pigen_symbol_declare(&sem_g, scope_g,
				PIGEN_SYMBOL_SIGNAL, td_g, name_g_g, name_g_g,
				&g_g, NULL) == PIGEN_DECLARE_OK);
			g_sig_g = pigen_signal_add(&sem_g, (pigen_syntax_id){4},
				module_g, g_g, td_g, pigen_semantic_scalar_shape(&sem_g),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INPUT, name_g_g);
			(void)g_sig_g;
			REQUIRE(pigen_symbol_declare(&sem_g, scope_g,
				PIGEN_SYMBOL_SIGNAL, td_g, name_f_g, name_f_g,
				&f_g, NULL) == PIGEN_DECLARE_OK);
			f_sig_g = pigen_signal_add(&sem_g, (pigen_syntax_id){5},
				module_g, f_g, td_g, pigen_semantic_scalar_shape(&sem_g),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INPUT, name_f_g);
			(void)f_sig_g;
			REQUIRE(pigen_symbol_declare(&sem_g, scope_g,
				PIGEN_SYMBOL_SIGNAL, td_g, name_c_g, name_c_g,
				&c_g, NULL) == PIGEN_DECLARE_OK);
			c_sig_g = pigen_signal_add(&sem_g, (pigen_syntax_id){6},
				module_g, c_g, td_g, pigen_semantic_scalar_shape(&sem_g),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INPUT, name_c_g);
			(void)c_sig_g;
			REQUIRE(pigen_symbol_declare(&sem_g, scope_g,
				PIGEN_SYMBOL_SIGNAL, td_g, name_p_g, name_p_g,
				&p_g, NULL) == PIGEN_DECLARE_OK);
			p_sig_g = pigen_signal_add(&sem_g, (pigen_syntax_id){7},
				module_g, p_g, td_g, pigen_semantic_scalar_shape(&sem_g),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
				PIGEN_SEMANTIC_INPUT, name_p_g);
			(void)p_sig_g;
			dom_g = pigen_clock_domain_intern(&sem_g, clk_g,
				PIGEN_SEMANTIC_POSEDGE);
			clk_e_g = pigen_expr_add_symbol(&sem_g, clk_g, td_g,
				name_clk_g);
			proc_id_g = pigen_process_add(&sem_g, (pigen_syntax_id){8},
				module_g, dom_g, clk_e_g, proc_g);
			dest_g = pigen_lvalue_resolve(&sem_g,
				pigen_expr_add_symbol(&sem_g, d_g, td_g, name_d_g));
			val_g = pigen_expr_add_integer(&sem_g, 1, td_g, name_d_g);
			/* The guard is built through the owner predicate API, not
			 * seeded: true AND (g == 1) AND (f == 0). The expected-0
			 * atom is the negation; the owner reports the two atoms in
			 * condition-index order, g before f. */
			true_g = pigen_predicate_true(&sem_g);
			REQUIRE(!IS_INVALID_ID(true_g));
			g_e = pigen_expr_add_symbol(&sem_g, g_g, td_g, name_g_g);
			f_e = pigen_expr_add_symbol(&sem_g, f_g, td_g, name_f_g);
			guard_g = pigen_predicate_and_condition(&sem_g, true_g, g_e, 1);
			guard_g = pigen_predicate_and_condition(&sem_g, guard_g, f_e, 0);
			REQUIRE(!IS_INVALID_ID(guard_g));
			guard_get = pigen_predicate_get(&sem_g, guard_g);
			REQUIRE(guard_get && guard_get->atom_count == 2 &&
				!guard_get->impossible);
			guard_atoms = pigen_predicate_atoms(&sem_g, guard_g);
			REQUIRE(guard_atoms &&
				guard_atoms[0].condition.index == g_e.index &&
				guard_atoms[0].expected == 1 &&
				guard_atoms[1].condition.index == f_e.index &&
				guard_atoms[1].expected == 0);
			/* Five distinct (signal, role) uses, no duplicates: the
			 * destination payload d is both written and read, c is the
			 * consumer, p the producer, and g and f are each read as
			 * guard conditions. */
			uses_g[0] = (pigen_transfer_signal_use){d_sig_g,
				PIGEN_TRANSFER_SIGNAL_WRITE | PIGEN_TRANSFER_SIGNAL_READ};
			uses_g[1] = (pigen_transfer_signal_use){c_sig_g,
				PIGEN_TRANSFER_CONSUMER};
			uses_g[2] = (pigen_transfer_signal_use){p_sig_g,
				PIGEN_TRANSFER_PRODUCER};
			uses_g[3] = (pigen_transfer_signal_use){g_sig_g,
				PIGEN_TRANSFER_SIGNAL_READ};
			uses_g[4] = (pigen_transfer_signal_use){f_sig_g,
				PIGEN_TRANSFER_SIGNAL_READ};
			tr_id_g = pigen_transfer_add(&sem_g, (pigen_syntax_id){9},
				module_g, proc_id_g, dest_g, val_g, guard_g, dom_g,
				uses_g, 5, tr_g);
			REQUIRE(!IS_INVALID_ID(tr_id_g));
			tr_get = pigen_transfer_get(&sem_g, tr_id_g);
			REQUIRE(tr_get && tr_get->guard.index == guard_g.index &&
				tr_get->signal_use_count == 5);
			/* RED entry-point gate: the stubbed lowering rejects today;
			 * this line runs only once the lowering lands, behind the
			 * first deliberate red above. Nothing is asserted after it
			 * in this child. */
			REQUIRE(pigen_lower_rtl_transfers(&lowering_g, module_g) == 0);

			/* The guard-atom and endpoint-conjunct assertions (7.1)-(7.6)
			 * pin the escape this witness exposes: its guard is the
			 * two-atom conjunction (g == 1 AND f == 0, the expected-0 atom
			 * the negation) and its transfer carries five distinct
			 * (signal, role) uses, so the shared fire identity must be the
			 * memoized conjunction of the two lowered guard atoms, the
			 * consumer-valid and producer-ready endpoint controls and
			 * nothing else. Staged RED behind the first deliberate red
			 * above; nothing here may add a second failure point. */
			{
				const pigen_semantic_transfer *tr_g_get;
				const pigen_transfer_signal_use *tr_g_uses;
				const pigen_rtl_update *upd_g;
				const pigen_rtl_equation *eq_g;
				pigen_rtl_expr_id upd_val_g;
				pigen_rtl_expr_id rdy_val_g;
				int *seen_u_g;
				int *seen_r_g;
				pigen_rtl_expr_id *arena_u_g;
				pigen_rtl_expr_id *arena_r_g;
				size_t cap_g;
				size_t u_cnt_g = 0;
				size_t r_cnt_g = 0;
				pigen_rtl_expr_id g_fire_g;
				pigen_rtl_expr_id g_neg_g;
				const pigen_rtl_expr *fire_rec_g;
				size_t fire_cc_g;
				const pigen_rtl_expr_id *fire_ch_g;
				size_t i;

				/* (7.1) Locate the transfer's single destination update and
				 * its single source-ready equation by endpoint identity and
				 * REQUIRE both carry a valid value: the entry contract
				 * publishes the fire identity on exactly these two records.
				 * A missing or empty record fails here, before any tree
				 * probe. */
				tr_g_get = pigen_transfer_get(&sem_g, tr_id_g);
				tr_g_uses = pigen_transfer_signal_uses(&sem_g, tr_id_g);
				REQUIRE(tr_g_get && tr_g_uses);
				upd_val_g = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
				rdy_val_g = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
				for (i = 0; i < rtl_g.update_count; i++) {
					upd_g = pigen_rtl_update_get(&rtl_g,
						(pigen_rtl_update_id){(uint32_t)i});
					if (upd_g &&
						upd_g->destination.index ==
							lowering_g.lowered_endpoints
								[d_sig_g.index].payload.index) {
						upd_val_g = upd_g->value;
						break;
					}
				}
				for (i = 0; i < rtl_g.equation_count; i++) {
					eq_g = pigen_rtl_equation_get(&rtl_g,
						(pigen_rtl_equation_id){(uint32_t)i});
					if (eq_g &&
						eq_g->destination.index ==
							lowering_g.lowered_endpoints
								[d_sig_g.index].input_ready.index) {
						rdy_val_g = eq_g->value;
						break;
					}
				}
				REQUIRE(!IS_INVALID_ID(upd_val_g) &&
					!IS_INVALID_ID(rdy_val_g));

				/* (7.2) The update value and the ready value share one
				 * non-constant fire-identity node (the family (1) check):
				 * a re-lowered copy leaves no shared non-constant node. */
				REQUIRE(rtest_shares_nonconst(&rtl_g, upd_val_g,
					rdy_val_g));

				/* Collect the distinct reachable node sets once, from both
				 * roots, with the file-local collector and an arena sized
				 * to the model's expression count: the endpoint-conjunct,
				 * guard-atom and dedup probes below all test membership
				 * against these two identity sets. */
				cap_g = rtl_g.expression_count + 1;
				seen_u_g = calloc(cap_g, sizeof(*seen_u_g));
				seen_r_g = calloc(cap_g, sizeof(*seen_r_g));
				arena_u_g = malloc(cap_g * sizeof(*arena_u_g));
				arena_r_g = malloc(cap_g * sizeof(*arena_r_g));
				REQUIRE(seen_u_g && seen_r_g && arena_u_g && arena_r_g);
				REQUIRE(rtest_collect_exprs(&rtl_g, upd_val_g, seen_u_g,
					arena_u_g, cap_g, &u_cnt_g) &&
					rtest_collect_exprs(&rtl_g, rdy_val_g, seen_r_g,
					arena_r_g, cap_g, &r_cnt_g));

				/* (7.3) Endpoint conjuncts: for EVERY distinct signal use
				 * the endpoint control the use contributes must be a
				 * reachable identity in BOTH sets - c's valid control for
				 * the consumer use, p's ready control for the producer
				 * use. A fire identity that forgets one endpoint control,
				 * or reaches it from only one side, fails here. */
				for (i = 0; i < tr_g_get->signal_use_count; i++) {
					pigen_rtl_expr_id ctl_g =
						(pigen_rtl_expr_id){PIGEN_INVALID_ID};
					int have_u = 0;
					int have_r = 0;
					size_t k;

					if (tr_g_uses[i].roles & PIGEN_TRANSFER_CONSUMER)
						ctl_g = lowering_g.lowered_endpoints
							[tr_g_uses[i].signal.index].valid;
					if (tr_g_uses[i].roles & PIGEN_TRANSFER_PRODUCER)
						ctl_g = lowering_g.lowered_endpoints
							[tr_g_uses[i].signal.index].ready;
					if (IS_INVALID_ID(ctl_g))
						continue;
					for (k = 0; k < u_cnt_g; k++)
						if (arena_u_g[k].index == ctl_g.index)
							have_u = 1;
					for (k = 0; k < r_cnt_g; k++)
						if (arena_r_g[k].index == ctl_g.index)
							have_r = 1;
					REQUIRE(have_u && have_r);
				}

				/* (7.4) Guard-atom coverage: EACH canonical atom's
				 * condition lowers to an identity reachable from BOTH
				 * sets; ADDITIONALLY both sets contain a UNARY node
				 * wrapping the f-condition (expected 0) lowered identity -
				 * the negation - while the g-condition (expected 1)
				 * identity is present with no such negation requirement.
				 * A fire identity that drops an atom, or passes the
				 * expected-0 atom through un-negated, fails here. */
				for (i = 0; i < guard_get->atom_count; i++) {
					pigen_rtl_expr_id atom_lo_g;
					int have_u = 0;
					int have_r = 0;
					size_t k;

					atom_lo_g = lowering_g.lowered_expressions
						[guard_atoms[i].condition.index];
					for (k = 0; k < u_cnt_g; k++)
						if (arena_u_g[k].index == atom_lo_g.index)
							have_u = 1;
					for (k = 0; k < r_cnt_g; k++)
						if (arena_r_g[k].index == atom_lo_g.index)
							have_r = 1;
					REQUIRE(!IS_INVALID_ID(atom_lo_g) && have_u &&
						have_r);
				}
				g_neg_g = lowering_g.lowered_expressions[f_e.index];
				REQUIRE(!IS_INVALID_ID(g_neg_g));
				REQUIRE(rtest_count_reachable_kind(&rtl_g, upd_val_g,
					PIGEN_RTL_EXPR_UNARY, g_neg_g) >= 1);
				REQUIRE(rtest_count_reachable_kind(&rtl_g, rdy_val_g,
					PIGEN_RTL_EXPR_UNARY, g_neg_g) >= 1);
				REQUIRE(rtest_count_reachable_kind(&rtl_g, upd_val_g,
					PIGEN_RTL_EXPR_BINARY, g_neg_g) == 0);
				REQUIRE(rtest_count_reachable_kind(&rtl_g, rdy_val_g,
					PIGEN_RTL_EXPR_BINARY, g_neg_g) == 0);

				/* (7.5) Endpoint-control dedup: the identity set contains
				 * c's valid control EXACTLY once and p's ready control
				 * EXACTLY once in each set - the one CONSUMER and the one
				 * PRODUCER use contribute their endpoint control once, not
				 * per occurrence, per side. A per-occurrence re-lowering
				 * (one conjunct copy per use, per side) inflates the count
				 * and fails here. The control is the constant literal the
				 * declaration lowering published, so a set that reaches the
				 * VALUE without the control identity (a re-interned copy)
				 * also fails. */
				REQUIRE(pigen_rtl_expr_get(&rtl_g,
					lowering_g.lowered_endpoints[c_sig_g.index].valid)
					&& pigen_rtl_expr_get(&rtl_g,
					lowering_g.lowered_endpoints[c_sig_g.index].valid)
					->kind == PIGEN_RTL_EXPR_INTEGER);
				REQUIRE(pigen_rtl_expr_get(&rtl_g,
					lowering_g.lowered_endpoints[p_sig_g.index].ready)
					&& pigen_rtl_expr_get(&rtl_g,
					lowering_g.lowered_endpoints[p_sig_g.index].ready)
					->kind == PIGEN_RTL_EXPR_INTEGER);
				REQUIRE(rtest_count_reachable_kind(&rtl_g, upd_val_g,
					PIGEN_RTL_EXPR_INTEGER, lowering_g.lowered_endpoints
						[c_sig_g.index].valid) == 1);
				REQUIRE(rtest_count_reachable_kind(&rtl_g, rdy_val_g,
					PIGEN_RTL_EXPR_INTEGER, lowering_g.lowered_endpoints
						[c_sig_g.index].valid) == 1);
				REQUIRE(rtest_count_reachable_kind(&rtl_g, upd_val_g,
					PIGEN_RTL_EXPR_INTEGER, lowering_g.lowered_endpoints
						[p_sig_g.index].ready) == 1);
				REQUIRE(rtest_count_reachable_kind(&rtl_g, rdy_val_g,
					PIGEN_RTL_EXPR_INTEGER, lowering_g.lowered_endpoints
						[p_sig_g.index].ready) == 1);

				/* (7.6) Conjunction shape: the shared fire-identity node
				 * itself - the ready equation's value per the entry
				 * contract, else the deepest common non-INTEGER ancestor
				 * of the two reachable sets - is a two-child logical AND,
				 * not a bare atom or a constant. The two sides of the
				 * conjunction are the guard-atom branch and the
				 * endpoint-control branch: each side must itself reach
				 * the g-condition (expected 1) lowered identity and the
				 * consumer-valid control. A fire identity that is a bare
				 * atom, a constant, or a re-association that loses an
				 * operand fails here. */
				g_fire_g = rdy_val_g;
				if (IS_INVALID_ID(g_fire_g) ||
					(pigen_rtl_expr_get(&rtl_g, g_fire_g)->kind
						== PIGEN_RTL_EXPR_INTEGER)) {
					g_fire_g = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
					for (i = 0; i < u_cnt_g; i++) {
						pigen_rtl_expr_id cand_g = arena_u_g[i];
						int in_r_g = 0;
						size_t k;

						for (k = 0; k < r_cnt_g; k++)
							if (arena_r_g[k].index ==
								cand_g.index)
								in_r_g = 1;
						if (in_r_g &&
							pigen_rtl_expr_get(&rtl_g, cand_g)->
							kind != PIGEN_RTL_EXPR_INTEGER)
							g_fire_g = cand_g;
					}
				}
				fire_rec_g = pigen_rtl_expr_get(&rtl_g, g_fire_g);
				REQUIRE(fire_rec_g &&
					fire_rec_g->kind == PIGEN_RTL_EXPR_BINARY &&
					fire_rec_g->child_count == 2 &&
					fire_rec_g->as.binary.resolution.operation
						.operator == PIGEN_BINARY_LOGICAL_AND);
				fire_ch_g = pigen_rtl_expr_children(&rtl_g, g_fire_g,
					&fire_cc_g);
				REQUIRE(fire_ch_g && fire_cc_g == 2);
				for (i = 0; i < fire_cc_g; i++) {
					int *seen_s_g;
					pigen_rtl_expr_id *arena_s_g;
					size_t s_cnt_g = 0;
					int have_atom = 0;
					int have_ctl = 0;
					pigen_rtl_expr_id atom_lo_g;
					pigen_rtl_expr_id ctl_g;
					size_t k;

					seen_s_g = calloc(cap_g, sizeof(*seen_s_g));
					arena_s_g = malloc(cap_g * sizeof(*arena_s_g));
					REQUIRE(seen_s_g && arena_s_g);
					REQUIRE(rtest_collect_exprs(&rtl_g, fire_ch_g[i],
						seen_s_g, arena_s_g, cap_g, &s_cnt_g));
					atom_lo_g = lowering_g.lowered_expressions
						[g_e.index];
					ctl_g = lowering_g.lowered_endpoints
						[c_sig_g.index].valid;
					for (k = 0; k < s_cnt_g; k++) {
						if (arena_s_g[k].index == atom_lo_g.index)
							have_atom = 1;
						if (arena_s_g[k].index == ctl_g.index)
							have_ctl = 1;
					}
					REQUIRE(have_atom && have_ctl);
					free(seen_s_g);
					free(arena_s_g);
				}

				free(seen_u_g);
				free(seen_r_g);
				free(arena_u_g);
				free(arena_r_g);
			}
			pigen_rtl_lowering_free(&lowering_g);
			pigen_free_rtl_model(&rtl_g);
			pigen_free_semantic_model(&sem_g);
			pigen_free_sources(&sources_g);
		}

		pigen_rtl_lowering_free(&lowering_t);
		pigen_free_rtl_model(&rtl_t);
		pigen_free_semantic_model(&sem_t);
		pigen_free_sources(&sources_t);
	}

	SECTION("t5-teardown") {
	/* free releases the (empty) maps and zeroes the record. */
	pigen_rtl_lowering_free(&lowering);
	REQUIRE(!lowering.semantics && !lowering.rtl);
	REQUIRE(!lowering.lowered_types &&
		!lowering.lowered_type_count && !lowering.lowered_type_capacity);
	REQUIRE(!lowering.lowered_expressions &&
		!lowering.lowered_expression_count &&
		!lowering.lowered_expression_capacity);
	}

	pigen_free_rtl_model(&rtl);
	pigen_free_semantic_model(&sem);
	pigen_free_sources(&sources);
	return check_finish();
}
