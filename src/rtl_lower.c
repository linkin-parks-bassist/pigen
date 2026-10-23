/* Owner-based type and expression lowering for the elastic RTL vertical
 * slice (Task 5 implementation).
 *
 * A lowering pass consumes validated data-type and constant-expression
 * identities from the semantic model and publishes the corresponding RTL
 * records into the RTL model, memoizing by source arena index so each source
 * identity is lowered exactly once. The width, signedness, state domain,
 * packed range shape, every explicit conversion and projection, and the child
 * order all come from the data-type and expression owners; nothing is
 * recomputed. A call that cannot be lowered reports the sentinel and
 * publishes no partial records: the type/expression arena counts and every
 * previously populated identity-memo slot are left exactly as they were, so a
 * later call on the same lowering is unaffected. */
#include <stdlib.h>
#include <string.h>

#include "pigen/rtl_lower.h"
#include "pigen/util.h"

#define INVALID_TYPE ((pigen_rtl_type_id){PIGEN_INVALID_ID})
#define INVALID_EXPR ((pigen_rtl_expr_id){PIGEN_INVALID_ID})
#define INVALID_ORIGIN ((pigen_source_span){{PIGEN_INVALID_ID}, 0, 0})

void pigen_rtl_lowering_init(pigen_rtl_lowering *lowering,
	pigen_semantic_model *semantics, pigen_rtl_model *rtl)
{
	memset(lowering, 0, sizeof(*lowering));
	lowering->semantics = semantics;
	lowering->rtl = rtl;
}

void pigen_rtl_lowering_free(pigen_rtl_lowering *lowering)
{
	free(lowering->lowered_types);
	free(lowering->lowered_expressions);
	memset(lowering, 0, sizeof(*lowering));
}

/* Grow an identity-memo slot array to at least INDEX+1 slots and zero the new
 * slots. The array is indexed by source arena index so a lookup is a single
 * load; growing never touches already-populated slots. */
static void *memo_reserve(void **slots, size_t element_size, size_t *count,
	size_t *capacity, size_t index)
{
	size_t required = index + 1;

	if (required <= *capacity)
		return *slots;
	{
		size_t new_capacity = *capacity ? *capacity : 8;
		char *base;

		while (new_capacity < required)
			new_capacity *= 2;
		*slots = pigen_resize(*slots, new_capacity * element_size);
		base = (char *)*slots + *capacity * element_size;
		for (size_t slot = *capacity; slot < new_capacity; slot++)
			((uint32_t *)base)[slot - *capacity] = PIGEN_INVALID_ID;
		*capacity = new_capacity;
		*count = required;
		return *slots;
	}
}

/* A constant-expression id is lowerable when it is present in the arena and
 * carries no symbolic (signal) reference. The lowering mirrors the owner's
 * resolved result type and keeps the owner's explicit conversion, select and
 * child order verbatim; it never re-derives a width, signedness or child. */
static int constant_is_lowerable(const pigen_semantic_model *sem,
	pigen_const_expr_id expression)
{
	const pigen_const_expr *known = pigen_const_expr_get(sem, expression);

	return known && !pigen_const_expr_is_symbolic(sem, expression);
}

/* Evaluate a concrete owner constant-expression bound to a signed value. */
static int bound_value(pigen_semantic_model *sem,
	pigen_const_expr_id expression, int64_t *value)
{
	uint64_t evaluated;

	if (!pigen_const_expr_evaluate_u64(sem, expression, &evaluated))
		return 0;
	*value = (int64_t)evaluated;
	return 1;
}

/* Build the descriptor the owner reports for one data type: signedness, state
 * domain, the evaluated packed width and the owner's packed range shape. Every
 * bound is a concrete value (expression invalid), so the descriptor never
 * depends on an RTL expression this call has not published. On success the
 * dimensions array is owned by the caller, who frees it when the intern fails. */
static int build_type_descriptor(pigen_semantic_model *sem,
	pigen_data_type_id type, pigen_rtl_type *descriptor,
	pigen_rtl_packed_dimension **staged_dimensions)
{
	const pigen_packed_dimension *owner_dimensions;
	pigen_const_expr_id width_expression;
	uint64_t width;
	size_t dimension_count;
	size_t i;

	*staged_dimensions = NULL;
	dimension_count = pigen_data_type_dimension_count(sem, type);
	owner_dimensions = pigen_data_type_dimensions(sem, type);
	if (dimension_count && !owner_dimensions)
		return 0;

	width_expression = pigen_data_type_packed_width(sem, type);
	if (width_expression.index == PIGEN_INVALID_ID ||
		!pigen_const_expr_evaluate_u64(sem, width_expression, &width))
		return 0;

	memset(descriptor, 0, sizeof(*descriptor));
	descriptor->width_expression = INVALID_EXPR;
	descriptor->signedness = pigen_data_type_signedness(sem, type);
	descriptor->state_domain = pigen_data_type_state_domain(sem, type);
	descriptor->width = width;
	descriptor->dimension_count = dimension_count;
	if (dimension_count)
	{
		pigen_rtl_packed_dimension *dimensions = pigen_resize(NULL,
			dimension_count * sizeof(*dimensions));

		for (i = 0; i < dimension_count; i++)
		{
			int64_t left;
			int64_t right;

			if (!bound_value(sem, owner_dimensions[i].left, &left) ||
				!bound_value(sem, owner_dimensions[i].right, &right))
			{
				free(dimensions);
				return 0;
			}
			dimensions[i].left = (pigen_rtl_bound){left, INVALID_EXPR};
			dimensions[i].right = (pigen_rtl_bound){right, INVALID_EXPR};
		}
		descriptor->dimensions = dimensions;
		*staged_dimensions = dimensions;
	}
	return 1;
}

/* One data-type identity, lowered once: memo lookup by source arena index, then
 * owner-driven descriptor build and intern. The interned id is memoized before
 * it is returned, so a repeat lowering is stable and grows nothing. */
pigen_rtl_type_id pigen_lower_rtl_type(pigen_rtl_lowering *lowering,
	pigen_data_type_id type)
{
	pigen_rtl_type descriptor;
	pigen_rtl_packed_dimension *staged_dimensions = NULL;
	pigen_rtl_type_id id;

	if (!lowering || !lowering->semantics || !lowering->rtl)
		return INVALID_TYPE;
	if (type.index == PIGEN_INVALID_ID ||
		!pigen_data_type_exists(lowering->semantics, type))
		return INVALID_TYPE;

	{
		pigen_rtl_type_id *slots = memo_reserve((void **)&lowering->lowered_types,
			sizeof(*slots),
			&lowering->lowered_type_count,
			&lowering->lowered_type_capacity, type.index);
		if (slots[type.index].index != PIGEN_INVALID_ID)
			return slots[type.index];
	}

	if (!build_type_descriptor(lowering->semantics, type, &descriptor,
		&staged_dimensions))
		return INVALID_TYPE;
	id = pigen_rtl_type_intern(lowering->rtl, &descriptor);
	if (id.index == PIGEN_INVALID_ID)
	{
		free(staged_dimensions);
		return INVALID_TYPE;
	}
	/* The intern copied the dimensions into model ownership on success. */
	free(staged_dimensions);
	lowering->lowered_types[type.index] = id;
	return id;
}

/* The owner's concrete value for a constant expression, as a non-negative
 * magnitude: integer and exact-integer constants. The owner owns the value and
 * the lowering only packs it into the RTL literal word. */
static int constant_value(const pigen_semantic_model *sem,
	const pigen_const_expr *known, uint64_t *value)
{
	switch (known->kind)
	{
	case PIGEN_CONST_EXPR_INTEGER:
		*value = known->as.integer;
		return 1;
	case PIGEN_CONST_EXPR_EXACT_INTEGER:
		return pigen_integer_to_u64(sem, known->as.exact_integer, value);
	default:
		return 0;
	}
}

/* The owner's one-bit value for a four-state bits constant, packed from its
 * bit states (state one sets the bit, least-significant first). The owner owns
 * the states; the lowering only packs them into the low word. */
static int constant_bits_value(const pigen_semantic_model *sem,
	const pigen_const_expr *known, pigen_const_expr_id expression,
	uint64_t *value)
{
	const pigen_bit_state *states = pigen_const_expr_bits(sem, expression);
	size_t count = known->as.bits.state_count;
	size_t i;

	*value = 0;
	if (!states)
		return 0;
	for (i = 0; i < count && i < 64; i++)
		if (states[i] == 1)
			*value |= (uint64_t)1 << i;
	return 1;
}

/* The owner's ordered child identities for a constant expression, copied into
 * a caller-owned array, or zero children for a literal. Returns 0 when the
 * kind is not lowerable as an RTL node. Child order is the owner's. */
static int constant_child_count(const pigen_const_expr *known, size_t *count)
{
	switch (known->kind)
	{
	case PIGEN_CONST_EXPR_UNARY:
	case PIGEN_CONST_EXPR_CONVERSION:
		*count = 1;
		return 1;
	case PIGEN_CONST_EXPR_BINARY:
	case PIGEN_CONST_EXPR_INDEX:
		*count = 2;
		return 1;
	case PIGEN_CONST_EXPR_CONDITIONAL:
	case PIGEN_CONST_EXPR_SELECT:
		*count = 3;
		return 1;
	case PIGEN_CONST_EXPR_CONCATENATION:
		*count = known->as.sequence.child_count;
		return 1;
	case PIGEN_CONST_EXPR_INTEGER:
	case PIGEN_CONST_EXPR_EXACT_INTEGER:
	case PIGEN_CONST_EXPR_BITS:
		*count = 0;
		return 1;
	default:
		return 0;
	}
}

static int constant_children(const pigen_semantic_model *sem,
	const pigen_const_expr *known, pigen_const_expr_id *children)
{
	size_t count;

	if (!constant_child_count(known, &count) || !children)
		return 0;
	switch (known->kind)
	{
	case PIGEN_CONST_EXPR_UNARY:
		children[0] = known->as.unary.operand;
		break;
	case PIGEN_CONST_EXPR_BINARY:
		children[0] = known->as.binary.left;
		children[1] = known->as.binary.right;
		break;
	case PIGEN_CONST_EXPR_CONDITIONAL:
		children[0] = known->as.conditional.condition;
		children[1] = known->as.conditional.when_true;
		children[2] = known->as.conditional.when_false;
		break;
	case PIGEN_CONST_EXPR_CONVERSION:
		children[0] = known->as.conversion.operand;
		break;
	case PIGEN_CONST_EXPR_INDEX:
		children[0] = known->as.index.base;
		children[1] = known->as.index.index;
		break;
	case PIGEN_CONST_EXPR_SELECT:
		children[0] = known->as.select.base;
		children[1] = known->as.select.left;
		children[2] = known->as.select.right;
		break;
	case PIGEN_CONST_EXPR_CONCATENATION:
	{
		const pigen_const_expr_id *owner_children = pigen_const_expr_children(
			sem, known->as.sequence.first_child, count);
		size_t i;

		if (!owner_children)
			return 0;
		for (i = 0; i < count; i++)
			children[i] = owner_children[i];
		break;
	}
	default:
		break;
	}
	return 1;
}

/* Publish the owner's constant-expression identity into the RTL arena: append
 * the node preserving the owner's kind, resolution, conversion, select and
 * child order. The result type and every lowered child are already resolved by
 * the caller, so a failure here (an uncomputable literal value) publishes no
 * new record. The memo slot is filled only on success. */
static pigen_rtl_expr_id publish_expression(pigen_rtl_lowering *lowering,
	size_t index, const pigen_const_expr *known, pigen_rtl_type_id result_type,
	const pigen_rtl_expr_id *children, size_t child_count)
{
	pigen_rtl_expr_id id = INVALID_EXPR;
	pigen_unary_resolution unary_resolution;
	pigen_binary_resolution binary_resolution;
	pigen_conditional_resolution conditional_resolution;

	unary_resolution = (pigen_unary_resolution){0};
	binary_resolution = (pigen_binary_resolution){0};
	conditional_resolution = (pigen_conditional_resolution){0};

	switch (known->kind)
	{
	case PIGEN_CONST_EXPR_INTEGER:
	case PIGEN_CONST_EXPR_EXACT_INTEGER:
	{
		uint64_t value = 0;

		if (constant_value(lowering->semantics, known, &value))
			id = pigen_rtl_expr_add_integer(lowering->rtl, result_type,
				value, INVALID_ORIGIN);
		break;
	}
	case PIGEN_CONST_EXPR_BITS:
	{
		uint64_t value = 0;

		if (constant_bits_value(lowering->semantics, known,
			(pigen_const_expr_id){index}, &value))
			id = pigen_rtl_expr_add_bits(lowering->rtl, result_type,
				value, INVALID_ORIGIN);
		break;
	}
	case PIGEN_CONST_EXPR_UNARY:
		id = pigen_rtl_expr_add_unary(lowering->rtl, result_type,
			&unary_resolution, children[0], INVALID_ORIGIN);
		break;
	case PIGEN_CONST_EXPR_BINARY:
		id = pigen_rtl_expr_add_binary(lowering->rtl, result_type,
			&binary_resolution, children[0], children[1], INVALID_ORIGIN);
		break;
	case PIGEN_CONST_EXPR_CONDITIONAL:
		id = pigen_rtl_expr_add_conditional(lowering->rtl, result_type,
			&conditional_resolution, children[0], children[1],
			children[2], INVALID_ORIGIN);
		break;
	case PIGEN_CONST_EXPR_CONVERSION:
		id = pigen_rtl_expr_add_conversion(lowering->rtl, result_type,
			&known->as.conversion.conversion, children[0], INVALID_ORIGIN);
		break;
	case PIGEN_CONST_EXPR_INDEX:
		id = pigen_rtl_expr_add_index(lowering->rtl, result_type,
			children[0], children[1], INVALID_ORIGIN);
		break;
	case PIGEN_CONST_EXPR_SELECT:
		id = pigen_rtl_expr_add_select(lowering->rtl, result_type,
			children[0], children[1], children[2], known->as.select.kind,
			INVALID_ORIGIN);
		break;
	case PIGEN_CONST_EXPR_CONCATENATION:
		id = pigen_rtl_expr_add_concatenation(lowering->rtl, result_type,
			children, child_count, INVALID_ORIGIN);
		break;
	default:
		break;
	}
	if (id.index != PIGEN_INVALID_ID)
		lowering->lowered_expressions[index] = id;
	return id;
}

pigen_rtl_expr_id pigen_lower_rtl_expression(pigen_rtl_lowering *lowering,
	pigen_const_expr_id expression)
{
	const pigen_semantic_model *sem;
	pigen_rtl_model *rtl;
	const pigen_const_expr *known;
	pigen_const_expr_id *source_children;
	pigen_rtl_expr_id *child_slots;
	pigen_rtl_type_id result_type;
	pigen_rtl_expr_id id;
	size_t child_count;
	size_t i;

	if (!lowering || !(sem = lowering->semantics) || !(rtl = lowering->rtl))
		return INVALID_EXPR;
	if (expression.index == PIGEN_INVALID_ID ||
		!constant_is_lowerable(sem, expression))
		return INVALID_EXPR;

	/* Memo lookup by source arena index: a repeat lowering is stable and grows
	 * nothing. */
	{
		pigen_rtl_expr_id *slots = memo_reserve(
			(void **)&lowering->lowered_expressions,
			sizeof(*slots),
			&lowering->lowered_expression_count,
			&lowering->lowered_expression_capacity, expression.index);
		if (slots[expression.index].index != PIGEN_INVALID_ID)
			return slots[expression.index];
	}

	known = pigen_const_expr_get(sem, expression);
	if (!constant_child_count(known, &child_count))
		return INVALID_EXPR;
	source_children = pigen_resize(NULL, child_count ?
		child_count * sizeof(*source_children) : 1);
	if (!constant_children(sem, known, source_children))
	{
		free(source_children);
		return INVALID_EXPR;
	}

	/* Phase 1 (no arena writes): every source child must be a concrete,
	 * lowerable owner identity. A child that is invalid or unbound fails the
	 * whole call before any RTL record is published, so the arenas and memo
	 * stay exactly as they were. */
	for (i = 0; i < child_count; i++)
		if (!constant_is_lowerable(sem, source_children[i]))
		{
			free(source_children);
			return INVALID_EXPR;
		}

	/* Phase 2 (publish): lower the result type, then the children bottom-up,
	 * then the node itself. Every child is already validated, so each lowers
	 * successfully; the node is appended last and memoized on success. */
	result_type = pigen_lower_rtl_type(lowering, known->data_type);
	if (result_type.index == PIGEN_INVALID_ID)
	{
		free(source_children);
		return INVALID_EXPR;
	}
	child_slots = pigen_resize(NULL, child_count ?
		child_count * sizeof(*child_slots) : 1);
	for (i = 0; i < child_count; i++)
	{
		child_slots[i] = pigen_lower_rtl_expression(lowering,
			source_children[i]);
		if (child_slots[i].index == PIGEN_INVALID_ID)
		{
			free(child_slots);
			free(source_children);
			return INVALID_EXPR;
		}
	}
	id = publish_expression(lowering, expression.index, known, result_type,
		child_slots, child_count);
	free(child_slots);
	free(source_children);
	return id;
}
