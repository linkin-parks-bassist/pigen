/* Owner-based type and expression lowering for the elastic RTL vertical
 * slice (Task 5 implementation).
 *
 * The type half (pigen_lower_rtl_type) is implemented: it validates the source
 * data-type identity against the semantic owner before touching any arena or
 * memo map, lowers the owner-reported signedness, state domain, width and
 * packed range shape into a single RTL type record, and memoizes the returned
 * handle by the source arena index so each identity lowers exactly once. A
 * failing identity publishes nothing: the RTL model and both identity memo
 * maps stay exactly as they were.
 *
 * The expression half (pigen_lower_rtl_expression) is implemented: it
 * validates the source constant-expression identity against the semantic
 * owner, lowers the owner's resolved result data type, lowers every owner
 * child bottom-up by identity in owner order, and publishes one RTL
 * expression record of the matching kind (integer, binary, conversion,
 * select, concatenation) keeping the owner's resolution/conversion/select
 * kind and child order. Each identity is memoized by its arena index so it
 * lowers exactly once, and a failing identity publishes nothing: the RTL
 * model and both identity memo maps stay exactly as they were. */
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

/* Grow the type memo map so that slot index < needed is addressable, keeping
 * every unpopulated slot a sentinel. Returns NULL on allocation failure. */
static int type_map_ensure(pigen_rtl_lowering *lowering, size_t needed)
{
	size_t capacity;
	pigen_rtl_type_id *map;

	if (lowering->lowered_type_count >= needed)
		return 1;
	capacity = lowering->lowered_type_capacity;
	if (!capacity)
		capacity = 8;
	while (capacity < needed)
		capacity *= 2;
	map = pigen_resize(lowering->lowered_types,
		capacity * sizeof(*map));
	if (!map)
		return 0;
	for (size_t i = lowering->lowered_type_count; i < capacity; i++)
		map[i] = (pigen_rtl_type_id){PIGEN_INVALID_ID};
	lowering->lowered_types = map;
	lowering->lowered_type_capacity = capacity;
	lowering->lowered_type_count = needed;
	return 1;
}

pigen_rtl_type_id pigen_lower_rtl_type(pigen_rtl_lowering *lowering,
	pigen_data_type_id type)
{
	pigen_semantic_model *sem;
	pigen_rtl_model *rtl;
	pigen_rtl_type descriptor;
	pigen_rtl_type_id result;
	const pigen_packed_dimension *owner_dimensions;
	pigen_rtl_packed_dimension *dimensions;
	pigen_const_expr_id width_expr;
	uint64_t width;
	pigen_signedness signedness;
	pigen_state_domain state_domain;
	size_t dimension_count;
	size_t i;

	if (!lowering || !lowering->semantics || !lowering->rtl)
		return (pigen_rtl_type_id){PIGEN_INVALID_ID};
	sem = lowering->semantics;
	rtl = lowering->rtl;

	/* Memo hit: the identity was already lowered; return the memoized handle
	 * with no further growth. */
	if (type.index != PIGEN_INVALID_ID &&
		lowering->lowered_type_count > type.index &&
		lowering->lowered_types[type.index].index != PIGEN_INVALID_ID)
		return lowering->lowered_types[type.index];

	/* Owner validation before publication: a missing identity publishes
	 * nothing and leaves both memo maps and the RTL model exactly as before. */
	if (type.index == PIGEN_INVALID_ID ||
		!pigen_data_type_exists(sem, type))
		return (pigen_rtl_type_id){PIGEN_INVALID_ID};

	signedness = pigen_data_type_signedness(sem, type);
	state_domain = pigen_data_type_state_domain(sem, type);
	if (pigen_data_type_numerical_interpretation(sem, type) ==
		PIGEN_NUMERICAL_EXACT_INTEGER)
	{
		/* An exact-integer type carries no packed width: derive its width
		 * from the owner's exact value instead of the packed-width query
		 * (which reports INVALID_ID for this constructor). A non-negative
		 * value lowers to a valid RTL type; a negative value reports the
		 * sentinel with no partial record. */
		pigen_integer_id exact_value =
			pigen_data_type_exact_value(sem, type);
		width = pigen_integer_unsigned_width(sem, exact_value);
		if (signedness < PIGEN_SIGN_IMPLICIT || signedness > PIGEN_SIGN_SIGNED ||
			(state_domain != PIGEN_DATA_TYPE_STATE_TWO &&
			state_domain != PIGEN_DATA_TYPE_STATE_FOUR) || !width)
			return (pigen_rtl_type_id){PIGEN_INVALID_ID};
	}
	else
	{
		width_expr = pigen_data_type_packed_width(sem, type);
		if (signedness < PIGEN_SIGN_IMPLICIT || signedness > PIGEN_SIGN_SIGNED ||
			(state_domain != PIGEN_DATA_TYPE_STATE_TWO &&
			state_domain != PIGEN_DATA_TYPE_STATE_FOUR) ||
			!pigen_const_expr_evaluate_u64(sem, width_expr, &width))
			return (pigen_rtl_type_id){PIGEN_INVALID_ID};
	}

	dimension_count = pigen_data_type_dimension_count(sem, type);
	owner_dimensions = pigen_data_type_dimensions(sem, type);
	if (dimension_count && !owner_dimensions)
		return (pigen_rtl_type_id){PIGEN_INVALID_ID};

	descriptor = (pigen_rtl_type){0};
	descriptor.signedness = signedness;
	descriptor.state_domain = state_domain;
	descriptor.width = width;
	descriptor.width_expression = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	descriptor.dimension_count = dimension_count;

	/* Copy the owner's packed range shape into model-owned bounds. A concrete
	 * bound keeps its evaluated value; a symbolic bound keeps its lowered child
	 * expression (the expression lowering stage publishes that child). */
	if (dimension_count)
	{
		if (dimension_count > SIZE_MAX / sizeof(*dimensions))
			return (pigen_rtl_type_id){PIGEN_INVALID_ID};
		dimensions = pigen_resize(NULL,
			dimension_count * sizeof(*dimensions));
		if (!dimensions)
			return (pigen_rtl_type_id){PIGEN_INVALID_ID};
		for (i = 0; i < dimension_count; i++)
		{
			uint64_t bound_value;

			if (pigen_const_expr_evaluate_u64(sem,
				owner_dimensions[i].left, &bound_value))
				dimensions[i].left = (pigen_rtl_bound){
					(int64_t)bound_value,
					(pigen_rtl_expr_id){PIGEN_INVALID_ID}};
			else
			{
				pigen_rtl_expr_id child =
					pigen_lower_rtl_expression(lowering,
						owner_dimensions[i].left);
				if (child.index == PIGEN_INVALID_ID)
				{
					free(dimensions);
					return (pigen_rtl_type_id){PIGEN_INVALID_ID};
				}
				dimensions[i].left = (pigen_rtl_bound){0, child};
			}
			if (pigen_const_expr_evaluate_u64(sem,
				owner_dimensions[i].right, &bound_value))
				dimensions[i].right = (pigen_rtl_bound){
					(int64_t)bound_value,
					(pigen_rtl_expr_id){PIGEN_INVALID_ID}};
			else
			{
				pigen_rtl_expr_id child =
					pigen_lower_rtl_expression(lowering,
						owner_dimensions[i].right);
				if (child.index == PIGEN_INVALID_ID)
				{
					free(dimensions);
					return (pigen_rtl_type_id){PIGEN_INVALID_ID};
				}
				dimensions[i].right = (pigen_rtl_bound){0, child};
			}
		}
		descriptor.dimensions = dimensions;
	}

	/* Publish one RTL type record and memoize it by the source arena index. */
	result = pigen_rtl_type_intern(rtl, &descriptor);
	if (dimension_count)
		free(dimensions);
	if (result.index == PIGEN_INVALID_ID)
		return (pigen_rtl_type_id){PIGEN_INVALID_ID};
	if (!type_map_ensure(lowering, type.index + 1) ||
		lowering->lowered_type_count <= type.index ||
		lowering->lowered_types[type.index].index != PIGEN_INVALID_ID)
		return (pigen_rtl_type_id){PIGEN_INVALID_ID};
	lowering->lowered_types[type.index] = result;
	return result;
}

/* Grow the expression memo map so that slot index < needed is addressable,
 * keeping every unpopulated slot a sentinel. Returns 1 on success. */
static int expr_map_ensure(pigen_rtl_lowering *lowering, size_t needed)
{
	size_t capacity;
	pigen_rtl_expr_id *map;

	if (lowering->lowered_expression_count >= needed)
		return 1;
	capacity = lowering->lowered_expression_capacity;
	if (!capacity)
		capacity = 8;
	while (capacity < needed)
		capacity *= 2;
	map = pigen_resize(lowering->lowered_expressions,
		capacity * sizeof(*map));
	if (!map)
		return 0;
	for (size_t i = lowering->lowered_expression_count; i < capacity; i++)
		map[i] = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	lowering->lowered_expressions = map;
	lowering->lowered_expression_capacity = capacity;
	lowering->lowered_expression_count = needed;
	return 1;
}

pigen_rtl_expr_id pigen_lower_rtl_expression(pigen_rtl_lowering *lowering,
	pigen_const_expr_id expression)
{
	pigen_semantic_model *sem;
	pigen_rtl_model *rtl;
	const pigen_const_expr *owner;
	pigen_rtl_type_id result_type;
	pigen_rtl_expr_id result;
	pigen_source_span origin;

	if (!lowering || !lowering->semantics || !lowering->rtl)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	sem = lowering->semantics;
	rtl = lowering->rtl;

	/* Memo hit: the identity was already lowered; return the memoized handle
	 * with no further growth. */
	if (expression.index != PIGEN_INVALID_ID &&
		lowering->lowered_expression_count > expression.index &&
		lowering->lowered_expressions[expression.index].index !=
		PIGEN_INVALID_ID)
		return lowering->lowered_expressions[expression.index];

	/* Owner validation before publication: a missing identity publishes
	 * nothing and leaves both memo maps and the RTL model exactly as before. */
	if (expression.index == PIGEN_INVALID_ID ||
		expression.index >= sem->constant_expression_count ||
		!(owner = pigen_const_expr_get(sem, expression)))
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};

	/* Lower the owner's resolved result data type first; a failure there
	 * propagates the sentinel with no partial records. */
	if (owner->data_type.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	result_type = pigen_lower_rtl_type(lowering, owner->data_type);
	if (result_type.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};

	/* Children are lowered recursively bottom-up by identity in owner order;
	 * every child must resolve before the parent record is published so a
	 * structural failure leaves no partial records. */
	origin = (pigen_source_span){
		(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
	switch (owner->kind) {
	case PIGEN_CONST_EXPR_INTEGER:
		/* A literal has no structural children; publish it once the result
		 * type is in place so any structural child can resolve it. */
		result = pigen_rtl_expr_add_integer(rtl, result_type,
			owner->as.integer, origin);
		break;
	case PIGEN_CONST_EXPR_EXACT_INTEGER:
	{
		/* An owner-constructable exact-integer constant lowers IDENTICALLY to
		 * the bare integer constant: one PIGEN_RTL_EXPR_INTEGER carrying the
		 * owner-reported non-negative value and the lowered exact-integer type.
		 * The result type (lowered above) already gates a negative value to the
		 * sentinel, so here the value always evaluates to a u64. */
		uint64_t exact_value;
		if (!pigen_const_expr_evaluate_u64(sem, expression, &exact_value))
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		result = pigen_rtl_expr_add_integer(rtl, result_type, exact_value,
			origin);
		break;
	}
	case PIGEN_CONST_EXPR_BINARY:
	{
		const pigen_binary_operation *op = &owner->as.binary.operation;
		pigen_binary_resolution resolution;
		pigen_rtl_expr_id left;
		pigen_rtl_expr_id right;

		/* The owner stores the binary operation; the RTL record keeps a full
		 * resolution. Rebuild it from the owner's operand conversions (a
		 * widening keeps the operands at their source types) plus the owner's
		 * operation, which carries the authoritative operator and result type. */
		resolution = (pigen_binary_resolution){
			{PIGEN_CONVERSION_IDENTITY, op->left_data_type,
				op->left_data_type},
			{PIGEN_CONVERSION_IDENTITY, op->right_data_type,
				op->right_data_type},
			*op};
		left = pigen_lower_rtl_expression(lowering, owner->as.binary.left);
		right = pigen_lower_rtl_expression(lowering, owner->as.binary.right);
		if (left.index == PIGEN_INVALID_ID ||
			right.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		result = pigen_rtl_expr_add_binary(rtl, result_type,
			&resolution, left, right, origin);
		break;
	}
	case PIGEN_CONST_EXPR_CONVERSION:
	{
		pigen_rtl_expr_id operand;

		operand = pigen_lower_rtl_expression(lowering,
			owner->as.conversion.operand);
		if (operand.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		result = pigen_rtl_expr_add_conversion(rtl, result_type,
			&owner->as.conversion.conversion, operand, origin);
		break;
	}
	case PIGEN_CONST_EXPR_SELECT:
	{
		pigen_rtl_expr_id base;
		pigen_rtl_expr_id left;
		pigen_rtl_expr_id right;

		base = pigen_lower_rtl_expression(lowering, owner->as.select.base);
		left = pigen_lower_rtl_expression(lowering, owner->as.select.left);
		right = pigen_lower_rtl_expression(lowering, owner->as.select.right);
		if (base.index == PIGEN_INVALID_ID ||
			left.index == PIGEN_INVALID_ID ||
			right.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		result = pigen_rtl_expr_add_select(rtl, result_type, base, left,
			right, owner->as.select.kind, origin);
		break;
	}
	case PIGEN_CONST_EXPR_CONCATENATION:
	{
		const pigen_const_expr_id *owner_children;
		size_t child_count;
		size_t i;
		pigen_rtl_expr_id *children;

		child_count = owner->as.sequence.child_count;
		owner_children = pigen_const_expr_children(sem,
			owner->as.sequence.first_child, child_count);
		if (child_count && !owner_children)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		if (child_count > SIZE_MAX / sizeof(*children))
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		children = pigen_resize(NULL,
			child_count * sizeof(*children));
		if (!children && child_count)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		for (i = 0; i < child_count; i++)
		{
			children[i] = pigen_lower_rtl_expression(lowering,
				owner_children[i]);
			if (children[i].index == PIGEN_INVALID_ID)
			{
				free(children);
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			}
		}
		result = pigen_rtl_expr_add_concatenation(rtl, result_type,
			children, child_count, origin);
		free(children);
		break;
	}
	default:
		/* Symbol, bit-state, width and other kinds are not lowered in this
		 * stage; an unbound signal is an error with no partial records. */
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	}

	if (result.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (!expr_map_ensure(lowering, expression.index + 1) ||
		lowering->lowered_expression_count <= expression.index ||
		lowering->lowered_expressions[expression.index].index !=
		PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	lowering->lowered_expressions[expression.index] = result;
	return result;
}
