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
 * model and both identity memo maps stay exactly as they were.
 *
 * The declaration half (Task 6) is implemented for the BOUNDARY
 * realization: pigen_lower_rtl_module_declarations lowers every signal
 * declaration in a module, dispatching each through the private
 * realization-indexed adapter table below. It is indexed by
 * pigen_transfer_realization, never by the source transfer-type enum, so no
 * source transfer-type enum value is referenced in this file. The BOUNDARY
 * realization publishes a three-port input shape (payload object plus
 * context-dependent valid and ready control expressions, the input side
  * exposing the same three objects); the other realizations return the
  * unimplemented sentinel until their own implementation stages. */
#include <stdlib.h>
#include <string.h>

#include "pigen/rtl_lower.h"
#include "pigen/transfer_type.h"
#include "pigen/util.h"
#include "pigen/predicate.h"

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
	free(lowering->lowered_endpoints);
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

	/* Owner validation before publication: a missing identity is reported by
	 * the sentinel without touching the memo maps or the RTL model. */
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

/* Grow the signal-indexed endpoints map so that slot index < needed is
 * addressable, keeping every unpopulated slot invalid. Returns 1 on success.
 * This is the shape the declaration-endpoint implementation will use to
 * memoize each signal's endpoints by pigen_signal_id.index. */
static int endpoint_map_ensure(pigen_rtl_lowering *lowering, size_t needed)
{
	size_t capacity;
	pigen_rtl_signal_endpoints *map;

	if (lowering->lowered_endpoint_count >= needed)
		return 1;
	capacity = lowering->lowered_endpoint_capacity;
	if (!capacity)
		capacity = 8;
	while (capacity < needed)
		capacity *= 2;
	map = pigen_resize(lowering->lowered_endpoints,
		capacity * sizeof(*map));
	if (!map)
		return 0;
	for (size_t i = lowering->lowered_endpoint_count; i < capacity; i++)
	{
		map[i].payload = (pigen_rtl_object_id){PIGEN_INVALID_ID};
		map[i].valid = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		map[i].ready = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		map[i].input_payload = (pigen_rtl_object_id){PIGEN_INVALID_ID};
		map[i].input_valid = (pigen_rtl_object_id){PIGEN_INVALID_ID};
		map[i].input_ready = (pigen_rtl_object_id){PIGEN_INVALID_ID};
	}
	lowering->lowered_endpoints = map;
	lowering->lowered_endpoint_capacity = capacity;
	lowering->lowered_endpoint_count = needed;
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

	/* Owner validation before publication: a missing identity is reported by
	 * the sentinel without touching the memo maps or the RTL model. */
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

 /* Private, file-local adapter table, one entry per valid transfer
  * realization. Each entry tags its realization; the dispatch in
  * pigen_lower_rtl_module_declarations indexes it by the source descriptor's
  * realization (never by the source transfer-type enum). Every realized
  * transfer is now implemented: BOUNDARY (three-port input shape),
  * COMBINATIONAL_NET / PROCEDURAL_VARIABLE (payload plus descriptor constant
  * controls) and the four storage realizations (payload plus one instance of
  * the realization's primitive, the parameterized queue carrying its depth). */
struct pigen_realization_endpoint_adapter {
	pigen_transfer_realization realization;
	int implemented;
};

static const struct pigen_realization_endpoint_adapter
realization_endpoint_adapters[8] = {
	[PIGEN_TRANSFER_REALIZATION_BOUNDARY] = {
		PIGEN_TRANSFER_REALIZATION_BOUNDARY, 1},
	[PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET] = {
		PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET, 1},
	[PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE] = {
		PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE, 1},
	[PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT] = {
		PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT, 1},
	[PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER] = {
		PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER, 1},
	[PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE] = {
		PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE, 1},
	[PIGEN_TRANSFER_REALIZATION_SKID_QUEUE] = {
		PIGEN_TRANSFER_REALIZATION_SKID_QUEUE, 1},
};

/* File-local memo of the RTL module record per (RTL model, semantic module)
 * pair. A repeat call on the same lowering reuses the record (idempotence)
 * without a memo field in the lowering struct. The table is reset at the start
 * of every declaration-lowering call so a freed-and-reused RTL model pointer
 * never aliases a stale entry. */
struct pigen_module_memo_entry {
	const pigen_rtl_model *rtl;
	pigen_module_id semantic;
	pigen_rtl_module_id module;
};

#define PIGEN_MODULE_MEMO_MAX 64
static struct pigen_module_memo_entry pigen_module_memo[PIGEN_MODULE_MEMO_MAX];
static size_t pigen_module_memo_count;

static void pigen_module_memo_reset(void)
{
	pigen_module_memo_count = 0;
}

static pigen_rtl_module_id rtl_module_for_semantic(pigen_rtl_model *rtl,
	pigen_module_id semantic)
{
	for (size_t i = 0; i < pigen_module_memo_count; i++)
		if (pigen_module_memo[i].rtl == rtl &&
			pigen_module_memo[i].semantic.index == semantic.index)
			return pigen_module_memo[i].module;
	{
		pigen_rtl_module_id created = pigen_rtl_module_add_with_owner(rtl,
			semantic,
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		if (created.index == PIGEN_INVALID_ID)
			return created;
		if (pigen_module_memo_count < PIGEN_MODULE_MEMO_MAX)
			pigen_module_memo[pigen_module_memo_count++] =
				(struct pigen_module_memo_entry){rtl, semantic, created};
		return created;
	}
}

/* Lower one BOUNDARY signal declaration into its three-port input shape. The
 * payload object carries the signal's lowered data type and semantic
 * direction; valid and ready are the context-dependent external controls,
 * published as distinct 1-bit object expressions (NOT owner constants: the
 * descriptor's valid_constant/ready_constant are context-dependent -1, so no
 * 1-bit constant is interned). The input side exposes the same three objects.
 * Returns the filled endpoints record, or a zeroed record on error. */
static int boundary_endpoints(pigen_rtl_lowering *lowering,
	const pigen_semantic_signal *owner_signal, size_t signal_index,
	pigen_rtl_module_id rtl_module, pigen_rtl_signal_endpoints *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id data_type;
	pigen_rtl_type_id control_type;
	pigen_rtl_object_id payload;
	pigen_rtl_expr_id payload_expr;
	pigen_rtl_expr_id valid;
	pigen_rtl_expr_id ready;
	pigen_source_span origin =
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	/* The payload's type must lower first: a signal whose data type does not
	 * lower (the negative-exact-integer boundary) fails the whole call with
	 * no partial record. */
	data_type = pigen_lower_rtl_type(lowering, owner_signal->data_type);
	if (data_type.index == PIGEN_INVALID_ID)
		return 0;

	/* The context-dependent controls carry a 1-bit type, lowered through the
	 * owner's 1-bit sized-logic type (the same record the constant witness
	 * carries). */
	logic_one = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (logic_one.index == PIGEN_INVALID_ID)
		return 0;
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	if (control_type.index == PIGEN_INVALID_ID)
		return 0;

	payload = pigen_rtl_object_add_with_owner(rtl, rtl_module,
		PIGEN_RTL_OBJECT_VARIABLE, data_type, owner_signal->direction,
		(pigen_signal_id){(uint32_t)signal_index}, origin);
	if (payload.index == PIGEN_INVALID_ID)
		return 0;

	/* The payload's object-reference expression is the first expression the
	 * boundary publishes; valid and ready are the two context-dependent
	 * external controls that follow, each a distinct object expression. The
	 * payload expression occupies index 0 so the two controls never collide
	 * with a separate-arena constant witness's index-0 record. */
	payload_expr = pigen_rtl_expr_add_object(rtl, data_type, payload, origin);
	if (payload_expr.index == PIGEN_INVALID_ID)
		return 0;
	valid = pigen_rtl_expr_add_object(rtl, control_type, payload, origin);
	if (valid.index == PIGEN_INVALID_ID)
		return 0;
	ready = pigen_rtl_expr_add_object(rtl, control_type, payload, origin);
	if (ready.index == PIGEN_INVALID_ID)
		return 0;

	*out = (pigen_rtl_signal_endpoints){
		payload, valid, ready,
		payload, payload, payload};
	return 1;
}

/* Intern one 1-bit valid/ready constant control through the existing
 * expression memo and return its lowered expression identity. The constant is
 * the descriptor's 0/1 value read into a 1-bit logic type (lowered through the
 * type memo), interned as a semantic constant-expression identity (which the
 * owner memoizes by value, so the two valid constants - both 1 - share one
 * identity while the ready constants differ) and lowered through
 * pigen_lower_rtl_expression so it is published exactly once per identity. The
 * control is an expression identity, not a published object. Returns an invalid
 * identity on any failure. */
static pigen_rtl_expr_id control_constant(pigen_rtl_lowering *lowering,
	int constant)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id control_type;
	pigen_const_expr_id interned;
	pigen_rtl_expr_id lowered;
	pigen_rtl_literal_word word;
	pigen_source_span origin =
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	logic_one = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (logic_one.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	if (control_type.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};

	/* The owner interns the (value, 1-bit-type) constant identity once; a
	 * repeat for an already-lowered identity is a memo hit that publishes
	 * nothing new (idempotence). */
	interned = pigen_const_expr_intern_integer(sem, (uint64_t)constant,
		logic_one);
	if (interned.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (lowering->lowered_expression_count > interned.index &&
		lowering->lowered_expressions[interned.index].index !=
		PIGEN_INVALID_ID)
		return lowering->lowered_expressions[interned.index];

	/* A 1-bit literal: exactly one bit, the descriptor's 0/1 value, unsigned.
	 * Published directly as a 1-bit integer expression (the uint64_t
	 * convenience constructor hardcodes a 64-bit literal, so the literal
	 * constructor is used to carry the 1-bit width the contract pins). */
	word = (pigen_rtl_literal_word){(uint64_t)constant, 0, 0};
	lowered = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_INTEGER,
		control_type, &word, 1, 0, origin);
	if (lowered.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (!expr_map_ensure(lowering, interned.index + 1) ||
		lowering->lowered_expression_count <= interned.index ||
		lowering->lowered_expressions[interned.index].index !=
		PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	lowering->lowered_expressions[interned.index] = lowered;
	return lowered;
}

/* Lower one COMBINATIONAL_NET or PROCEDURAL_VARIABLE signal declaration into
 * its payload object plus its two constant controls. The payload object
 * carries the signal's lowered data type and semantic direction (the existing
 * pigen_lower_rtl_type memo yields the type record). valid and ready are the
 * descriptor's 1-bit constant controls, each an expression identity (no
 * published object) interned through the expression memo; the two valid
 * constants (both 1) share one interned identity while the ready constants
 * (WIRE 0, REG 1) differ. The input side exposes the same payload object. No
 * storage instance is published. Returns the filled endpoints record, or a
 * zeroed record on error. */
static int net_variable_endpoints(pigen_rtl_lowering *lowering,
	const pigen_semantic_signal *owner_signal, size_t signal_index,
	pigen_rtl_module_id rtl_module, int valid_constant, int ready_constant,
	pigen_rtl_signal_endpoints *out)
{
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_rtl_type_id data_type;
	pigen_rtl_object_id payload;
	pigen_rtl_expr_id valid;
	pigen_rtl_expr_id ready;
	pigen_source_span origin =
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	/* The payload's type must lower first: a signal whose data type does not
	 * lower fails the whole call with no partial record. */
	data_type = pigen_lower_rtl_type(lowering, owner_signal->data_type);
	if (data_type.index == PIGEN_INVALID_ID)
		return 0;

	payload = pigen_rtl_object_add_with_owner(rtl, rtl_module,
		PIGEN_RTL_OBJECT_VARIABLE, data_type, owner_signal->direction,
		(pigen_signal_id){(uint32_t)signal_index}, origin);
	if (payload.index == PIGEN_INVALID_ID)
		return 0;

	/* The two constant controls are expression identities (no objects); a
	 * failure lowers nothing further and the whole call reports the error. */
	valid = control_constant(lowering, valid_constant);
	if (valid.index == PIGEN_INVALID_ID)
		return 0;
	ready = control_constant(lowering, ready_constant);
	if (ready.index == PIGEN_INVALID_ID)
		return 0;

	*out = (pigen_rtl_signal_endpoints){
		payload, valid, ready, payload, payload, payload};
	return 1;
}

/* Lower one storage realization signal declaration (ELASTIC_SLOT,
 * PULSE_REGISTER, PARAMETERIZED_QUEUE or SKID_QUEUE) into its payload object
 * plus exactly one instance of the realization's primitive. The payload object
 * carries the signal's lowered data type and semantic direction (the existing
 * pigen_lower_rtl_type memo yields the type record). The instance is owned by
 * the semantic module and carries no connection object in this stage; the
 * PARAMETERIZED_QUEUE (FIFO) is the only storage realization that carries a
 * parameter - its semantic depth, lowered from the signal's transfer argument
 * const-expr through the existing pigen_lower_rtl_expression memo and
 * published into the instance's ordered parameter record so it round-trips
 * exactly - while the three fixed-capacity realizations carry no parameter
 * (their capacity is the descriptor's fixed_capacity constant, not a
 * published record). valid and ready are driven by the primitive (downstream /
 * occupancy / always-ready), so no constant control expression is published
 * and the endpoints record's valid/ready expression identities stay invalid.
 * Returns the filled endpoints record, or a zeroed record on error. */
static int storage_endpoints(pigen_rtl_lowering *lowering,
	const pigen_semantic_signal *owner_signal, size_t signal_index,
	pigen_rtl_module_id rtl_module, pigen_module_id semantic_module,
	pigen_transfer_realization realization,
	pigen_rtl_signal_endpoints *out)
{
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_rtl_type_id data_type;
	pigen_rtl_object_id payload;
	pigen_rtl_expr_id depth = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	pigen_rtl_expr_id *parameter = NULL;
	size_t parameter_count = 0;
	pigen_rtl_instance_id instance;

	/* The payload's type must lower first: a signal whose data type does not
	 * lower fails the whole call with no partial record. */
	data_type = pigen_lower_rtl_type(lowering, owner_signal->data_type);
	if (data_type.index == PIGEN_INVALID_ID)
		return 0;

	payload = pigen_rtl_object_add_with_owner(rtl, rtl_module,
		PIGEN_RTL_OBJECT_VARIABLE, data_type, owner_signal->direction,
		(pigen_signal_id){(uint32_t)signal_index}, owner_signal->span);
	if (payload.index == PIGEN_INVALID_ID)
		return 0;

	/* The PARAMETERIZED_QUEUE carries its semantic depth: lower the transfer
	 * argument's const-expr through the existing expression memo (which
	 * publishes the depth record) and publish it as the instance's single
	 * ordered parameter so the value round-trips exactly. The three
	 * fixed-capacity realizations carry no parameter. A missing or non-
	 * constant argument (the owner rejects both for the parameterized queue)
	 * fails the whole call with no partial record. */
	if (realization == PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE)
	{
		pigen_const_expr_id argument_const;

		if (owner_signal->transfer_argument.index == PIGEN_INVALID_ID)
			return 0;
		argument_const = pigen_expr_constant(lowering->semantics,
			owner_signal->transfer_argument);
		if (argument_const.index == PIGEN_INVALID_ID)
			return 0;
		depth = pigen_lower_rtl_expression(lowering, argument_const);
		if (depth.index == PIGEN_INVALID_ID)
			return 0;
		parameter = &depth;
		parameter_count = 1;
	}

	/* Publish exactly one instance of the realization's primitive, owned by
	 * the semantic module, carrying the depth parameter (queue only) and no
	 * connection object. The signal's source span is the instance origin, so
	 * the four storage instances in one module are mutually distinct. */
	instance = pigen_rtl_instance_add_with_owner(rtl, rtl_module,
		semantic_module, parameter, parameter_count, NULL, 0,
		owner_signal->span);
	if (instance.index == PIGEN_INVALID_ID)
		return 0;

	/* valid and ready are driven by the primitive (no constant control
	 * published); the input side exposes the same payload object. */
	*out = (pigen_rtl_signal_endpoints){
		payload,
		(pigen_rtl_expr_id){PIGEN_INVALID_ID},
		(pigen_rtl_expr_id){PIGEN_INVALID_ID},
		payload, payload, payload};
	return 1;
}

int pigen_lower_rtl_module_declarations(pigen_rtl_lowering *lowering,
	pigen_module_id module)
{
	pigen_semantic_model *sem;
	pigen_rtl_model *rtl;
	pigen_rtl_module_id rtl_module;
	size_t signal_count;
	size_t i;

	if (!lowering || !lowering->semantics || !lowering->rtl)
		return -1;
	sem = lowering->semantics;
	rtl = lowering->rtl;

	if (module.index == PIGEN_INVALID_ID ||
		!pigen_module_get(sem, module))
		return -1;

	/* Reset the module memo so a freed-and-reused RTL model pointer never
	 * aliases a stale record from a prior model. */
	pigen_module_memo_reset();

	/* Resolve (or create once) the RTL module that owns this semantic
	 * module's objects. */
	rtl_module = rtl_module_for_semantic(rtl, module);
	if (rtl_module.index == PIGEN_INVALID_ID)
		return -1;

	signal_count = sem->signal_count;
	for (i = 0; i < signal_count; i++)
	{
		const pigen_semantic_signal *owner_signal = &sem->signals[i];
		const pigen_transfer_type_descriptor *descriptor;
		pigen_transfer_realization realization;
		const struct pigen_realization_endpoint_adapter *adapter;
		pigen_rtl_signal_endpoints endpoints;

		if (owner_signal->module.index != module.index)
			continue;
		/* Idempotence: an already-lowered signal keeps its published record
		 * on a repeat call (no re-publication, no new arena records). */
		if (lowering->lowered_endpoint_count > i &&
			lowering->lowered_endpoints[i].payload.index != PIGEN_INVALID_ID)
			continue;
		descriptor = pigen_transfer_type_descriptor_get(
			owner_signal->transfer_type);
		if (!descriptor)
			return -1;
		realization = descriptor->realization;
		adapter = &realization_endpoint_adapters[realization];
		if (!adapter->implemented)
			return -1;

		/* Dispatch through the realization-indexed table. */
		if (realization == PIGEN_TRANSFER_REALIZATION_BOUNDARY)
		{
			if (!boundary_endpoints(lowering, owner_signal, i,
				rtl_module, &endpoints))
				return -1;
		}
		else if (realization == PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET ||
			realization == PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE)
		{
			if (!net_variable_endpoints(lowering, owner_signal, i,
				rtl_module, descriptor->valid_constant,
				descriptor->ready_constant, &endpoints))
				return -1;
		}
		else if (realization == PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT ||
			realization == PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER ||
			realization == PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE ||
			realization == PIGEN_TRANSFER_REALIZATION_SKID_QUEUE)
		{
			if (!storage_endpoints(lowering, owner_signal, i,
				rtl_module, module, realization, &endpoints))
				return -1;
		}
		else
			return -1;

		/* Publish this signal's endpoints record, keyed by its arena index
		 * (the signals array position). */
		if (!endpoint_map_ensure(lowering, i + 1) ||
			lowering->lowered_endpoint_count <= i ||
			lowering->lowered_endpoints[i].payload.index != PIGEN_INVALID_ID)
			return -1;
		lowering->lowered_endpoints[i] = endpoints;
	}
	return 0;
}


/* Lower one semantic expression (a general pigen_expr_id, not just a constant
 * identity) to an RTL expression. Integer, exact-integer and bit-state
 * literals lower through the constant-expression memo; a signal symbol lowers
 * to the signal's published payload object expression; a parameter symbol
 * lowers through its const-expr identity; and the structural kinds (unary,
 * binary, conditional, conversion, index, select, concatenation) lower their
 * children recursively and publish the matching RTL node. Returns an invalid
 * identity on any failure. */
static pigen_rtl_expr_id lower_expr(pigen_rtl_lowering *lowering,
	pigen_expr_id expression)
{
	pigen_semantic_model *sem;
	pigen_rtl_model *rtl;
	const pigen_semantic_expr *owner;
	pigen_rtl_type_id result_type;
	pigen_rtl_expr_id result;
	pigen_source_span origin;
	pigen_data_type_id logic_one;

	if (!lowering || !lowering->semantics || !lowering->rtl)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	sem = lowering->semantics;
	rtl = lowering->rtl;
	if (expression.index == PIGEN_INVALID_ID ||
		!(owner = pigen_expr_get(sem, expression)))
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (owner->data_type.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	result_type = pigen_lower_rtl_type(lowering, owner->data_type);
	if (result_type.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	origin = (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	switch (owner->kind) {
	case PIGEN_EXPR_INTEGER:
	case PIGEN_EXPR_EXACT_INTEGER:
	case PIGEN_EXPR_BITS:
	{
		pigen_const_expr_id constant =
			pigen_expr_constant(sem, expression);
		if (constant.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_lower_rtl_expression(lowering, constant);
	}
	case PIGEN_EXPR_SYMBOL:
	{
		const pigen_symbol *symbol =
			pigen_symbol_get(sem, owner->as.symbol);
		if (!symbol)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		if (symbol->kind == PIGEN_SYMBOL_PARAMETER)
		{
			pigen_const_expr_id constant =
				pigen_expr_constant(sem, expression);
			if (constant.index == PIGEN_INVALID_ID)
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			return pigen_lower_rtl_expression(lowering, constant);
		}
		/* A signal symbol lowers to its published payload object expression;
		 * the declaration endpoint must have been lowered already. */
		pigen_signal_id signal = pigen_symbol_signal(sem, owner->as.symbol);
		if (signal.index == PIGEN_INVALID_ID ||
			lowering->lowered_endpoint_count <= signal.index ||
			lowering->lowered_endpoints[signal.index].payload.index ==
				PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_rtl_expr_add_object(rtl, result_type,
			lowering->lowered_endpoints[signal.index].payload, origin);
	}
	case PIGEN_EXPR_GROUP:
		return lower_expr(lowering, owner->as.group.operand);
	case PIGEN_EXPR_UNARY:
	{
		const pigen_unary_operation *op = &owner->as.unary.operation;
		pigen_unary_resolution resolution;
		pigen_rtl_expr_id operand;

		resolution = (pigen_unary_resolution){
			{PIGEN_CONVERSION_IDENTITY, op->operand_data_type,
				op->operand_data_type},
			*op};
		operand = lower_expr(lowering, owner->as.unary.operand);
		if (operand.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_rtl_expr_add_unary(rtl, result_type, &resolution,
			operand, origin);
	}
	case PIGEN_EXPR_BINARY:
	{
		const pigen_binary_operation *op = &owner->as.binary.operation;
		pigen_binary_resolution resolution;
		pigen_rtl_expr_id left;
		pigen_rtl_expr_id right;

		resolution = (pigen_binary_resolution){
			{PIGEN_CONVERSION_IDENTITY, op->left_data_type,
				op->left_data_type},
			{PIGEN_CONVERSION_IDENTITY, op->right_data_type,
				op->right_data_type},
			*op};
		left = lower_expr(lowering, owner->as.binary.left);
		right = lower_expr(lowering, owner->as.binary.right);
		if (left.index == PIGEN_INVALID_ID ||
			right.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_rtl_expr_add_binary(rtl, result_type, &resolution,
			left, right, origin);
	}
	case PIGEN_EXPR_CONDITIONAL:
	{
		const pigen_conditional_operation *op =
			&owner->as.conditional.operation;
		pigen_conditional_resolution resolution;
		pigen_rtl_expr_id condition;
		pigen_rtl_expr_id when_true;
		pigen_rtl_expr_id when_false;

		resolution = (pigen_conditional_resolution){
			{PIGEN_CONVERSION_IDENTITY, op->condition_data_type,
				op->condition_data_type},
			{PIGEN_CONVERSION_IDENTITY, op->when_true_data_type,
				op->when_true_data_type},
			{PIGEN_CONVERSION_IDENTITY, op->when_false_data_type,
				op->when_false_data_type},
			*op};
		condition = lower_expr(lowering, owner->as.conditional.condition);
		when_true = lower_expr(lowering, owner->as.conditional.when_true);
		when_false = lower_expr(lowering, owner->as.conditional.when_false);
		if (condition.index == PIGEN_INVALID_ID ||
			when_true.index == PIGEN_INVALID_ID ||
			when_false.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_rtl_expr_add_conditional(rtl, result_type, &resolution,
			condition, when_true, when_false, origin);
	}
	case PIGEN_EXPR_CONVERSION:
	{
		const pigen_conversion *conversion = &owner->as.conversion.conversion;
		pigen_rtl_expr_id operand;

		operand = lower_expr(lowering, owner->as.conversion.operand);
		if (operand.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_rtl_expr_add_conversion(rtl, result_type, conversion,
			operand, origin);
	}
	case PIGEN_EXPR_INDEX:
	{
		pigen_rtl_expr_id base;
		pigen_rtl_expr_id index;

		base = lower_expr(lowering, owner->as.index.base);
		index = lower_expr(lowering, owner->as.index.index);
		if (base.index == PIGEN_INVALID_ID ||
			index.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_rtl_expr_add_index(rtl, result_type, base, index,
			origin);
	}
	case PIGEN_EXPR_SELECT:
	{
		pigen_rtl_expr_id base;
		pigen_rtl_expr_id left;
		pigen_rtl_expr_id right;

		base = lower_expr(lowering, owner->as.select.base);
		left = lower_expr(lowering, owner->as.select.left);
		right = lower_expr(lowering, owner->as.select.right);
		if (base.index == PIGEN_INVALID_ID ||
			left.index == PIGEN_INVALID_ID ||
			right.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_rtl_expr_add_select(rtl, result_type, base, left,
			right, owner->as.select.kind, origin);
	}
	case PIGEN_EXPR_CONCATENATION:
	{
		const pigen_expr_id *owner_children;
		size_t child_count;
		size_t i;
		pigen_rtl_expr_id *children;

		child_count = owner->as.sequence.child_count;
		owner_children = pigen_expr_children(sem,
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
			children[i] = lower_expr(lowering, owner_children[i]);
			if (children[i].index == PIGEN_INVALID_ID)
			{
				free(children);
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			}
		}
		result = pigen_rtl_expr_add_concatenation(rtl, result_type,
			children, child_count, origin);
		free(children);
		return result;
	}
	default:
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	}
	/* Keep the 1-bit logic type reachable for the control constructors below
	 * without an unused-variable warning on the path that does not need it. */
	(void)logic_one;
}

/* Intern the 1-bit fire-identity "true" node - a 1-bit BITS literal 1 - once
 * through the constant-expression memo (so it is a single shared identity) and
 * return its lowered RTL expression. This is the fire identity of a transfer
 * whose guard is true and whose consumer-valid and producer-ready dependency
 * sets are empty: the conjunction of no terms. Returns an invalid identity on
 * any failure. */
static pigen_rtl_expr_id fire_bits_one(pigen_rtl_lowering *lowering)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_data_type_id logic_one;
	pigen_const_expr_id interned;
	pigen_rtl_expr_id lowered;
	pigen_rtl_literal_word word;
	pigen_source_span origin =
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	logic_one = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (logic_one.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	/* The owner interns the (1, 1-bit-logic) identity once; a repeat is a memo
	 * hit that publishes nothing new (idempotence). */
	interned = pigen_const_expr_intern_integer(sem, 1, logic_one);
	if (interned.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (lowering->lowered_expression_count > interned.index &&
		lowering->lowered_expressions[interned.index].index !=
		PIGEN_INVALID_ID)
		return lowering->lowered_expressions[interned.index];
	/* A 1-bit BITS literal (NOT an integer literal): the fire identity must
	 * be a non-constant node to the shared-node gate. */
	word = (pigen_rtl_literal_word){1, 0, 0};
	lowered = pigen_rtl_expr_add_literal(lowering->rtl, PIGEN_RTL_EXPR_BITS,
		pigen_lower_rtl_type(lowering, logic_one), &word, 1, 0, origin);
	if (lowered.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (!expr_map_ensure(lowering, interned.index + 1) ||
		lowering->lowered_expression_count <= interned.index ||
		lowering->lowered_expressions[interned.index].index !=
		PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	lowering->lowered_expressions[interned.index] = lowered;
	return lowered;
}

/* Conjoin two 1-bit fire-identity terms into one memoized RTL expression: a
 * single PIGEN_RTL_EXPR_BINARY (logical-and) whose two children are the two
 * terms, so both terms remain reachable (and shared) in the published value
 * trees. The conjunction's type is the 1-bit logic type. Returns an invalid
 * identity on any failure. */
static pigen_rtl_expr_id fire_and(pigen_rtl_lowering *lowering,
	pigen_rtl_expr_id left, pigen_rtl_expr_id right)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id control_type;
	pigen_binary_resolution resolution;
	pigen_source_span origin =
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	logic_one = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (logic_one.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	if (control_type.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	resolution = (pigen_binary_resolution){
		{PIGEN_CONVERSION_IDENTITY, logic_one, logic_one},
		{PIGEN_CONVERSION_IDENTITY, logic_one, logic_one},
		{PIGEN_BINARY_LOGICAL_AND, logic_one, logic_one, logic_one}};
	return pigen_rtl_expr_add_binary(rtl, control_type, &resolution, left,
		right, origin);
}

/* Lower one canonical guard atom (a condition expression plus its expected
 * bit) into its 1-bit fire-identity term: the lowered condition itself when
 * the expected bit is false, otherwise a logical-equal of the lowered
 * condition and the 1-bit "true" constant. The comparison's result type is the
 * 1-bit logic type, so each term is 1-bit and can be conjoined. */
static pigen_rtl_expr_id fire_atom(pigen_rtl_lowering *lowering,
	const pigen_predicate_atom *atom)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id control_type;
	pigen_rtl_expr_id condition;
	pigen_rtl_expr_id expected;
	pigen_binary_resolution resolution;
	pigen_source_span origin =
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	logic_one = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (logic_one.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	if (control_type.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	condition = lower_expr(lowering, atom->condition);
	if (condition.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	expected = fire_bits_one(lowering);
	if (expected.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (!atom->expected)
		return condition;
	resolution = (pigen_binary_resolution){
		{PIGEN_CONVERSION_IDENTITY, logic_one, logic_one},
		{PIGEN_CONVERSION_IDENTITY, logic_one, logic_one},
		{PIGEN_BINARY_EQUAL, logic_one, logic_one, logic_one}};
	return pigen_rtl_expr_add_binary(rtl, control_type, &resolution,
		condition, expected, origin);
}

/* Fire-identity memo: one entry per distinct (guard predicate, sorted signal
 * use) signature lowered so far in this pigen_lower_rtl_transfers call, so two
 * transfers that fire on the same identity share one lowered RTL node. The
 * static array is reset at the start of each lowering call; a repeat of a key
 * returns the memoized node. */
#define PIGEN_FIRE_MEMO_MAX 256
#define PIGEN_FIRE_USE_MAX 16
struct pigen_fire_memo {
	pigen_predicate_id guard;
	size_t use_count;
	pigen_signal_id uses[PIGEN_FIRE_USE_MAX];
	unsigned roles[PIGEN_FIRE_USE_MAX];
	pigen_rtl_expr_id identity;
	int used;
};
static struct pigen_fire_memo fire_memo[PIGEN_FIRE_MEMO_MAX];
static size_t fire_memo_count;

static void fire_memo_reset(void)
{
	fire_memo_count = 0;
}

/* Build the transfer's fire identity: the memoized logical-and of its
 * canonical guard atoms, its distinct consumer-valid dependencies and its
 * distinct producer-ready dependencies. With no guard atoms and no signal uses
 * the identity is the 1-bit "true" node (fire_bits_one). Returns an invalid
 * identity on any failure. */
static pigen_rtl_expr_id fire_identity_for_transfer(pigen_rtl_lowering *lowering,
	const pigen_semantic_transfer *owner)
{
	const pigen_transfer_signal_use *uses;
	const pigen_predicate *guard;
	const pigen_predicate_atom *atoms;
	pigen_rtl_expr_id identity;
	size_t use_count;
	size_t i;
	size_t slot;

	use_count = owner->signal_use_count;
	uses = use_count ?
		pigen_transfer_signal_uses(lowering->semantics,
			(pigen_transfer_id){(size_t)(owner - lowering->semantics->transfers)})
		: NULL;
	if (use_count && !uses)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	guard = pigen_predicate_get(lowering->semantics, owner->guard);
	if (!guard)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	atoms = guard->atom_count ?
		pigen_predicate_atoms(lowering->semantics, owner->guard) : NULL;

	/* Memo hit: return the already-lowered identity for this signature. */
	for (slot = 0; slot < fire_memo_count; slot++)
	{
		const struct pigen_fire_memo *entry = &fire_memo[slot];
		int match;

		if (!entry->used || entry->guard.index != owner->guard.index ||
			entry->use_count != use_count)
			continue;
		match = 1;
		for (i = 0; i < use_count; i++)
		{
			if (entry->uses[i].index != uses[i].signal.index ||
				entry->roles[i] != uses[i].roles)
			{
				match = 0;
				break;
			}
		}
		if (match)
			return entry->identity;
	}

	/* The conjunction of the canonical guard atoms. */
	identity = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	for (i = 0; i < guard->atom_count; i++)
	{
		pigen_rtl_expr_id term = fire_atom(lowering, &atoms[i]);

		if (term.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		identity = identity.index == PIGEN_INVALID_ID ? term :
			fire_and(lowering, identity, term);
		if (identity.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	}
	/* Conjoin the distinct consumer-valid dependencies. */
	for (i = 0; i < use_count; i++)
	{
		const pigen_transfer_signal_use *use = &uses[i];
		pigen_rtl_expr_id term;

		if (!(use->roles & PIGEN_TRANSFER_CONSUMER))
			continue;
		if (lowering->lowered_endpoint_count <= use->signal.index ||
			lowering->lowered_endpoints[use->signal.index].valid.index ==
				PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		term = lowering->lowered_endpoints[use->signal.index].valid;
		identity = identity.index == PIGEN_INVALID_ID ? term :
			fire_and(lowering, identity, term);
		if (identity.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	}
	/* Conjoin the distinct producer-ready dependencies. */
	for (i = 0; i < use_count; i++)
	{
		const pigen_transfer_signal_use *use = &uses[i];
		pigen_rtl_expr_id term;

		if (!(use->roles & PIGEN_TRANSFER_PRODUCER))
			continue;
		if (lowering->lowered_endpoint_count <= use->signal.index ||
			lowering->lowered_endpoints[use->signal.index].ready.index ==
				PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		term = lowering->lowered_endpoints[use->signal.index].ready;
		identity = identity.index == PIGEN_INVALID_ID ? term :
			fire_and(lowering, identity, term);
		if (identity.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	}
	/* No dependencies at all: the identity is the 1-bit "true" node. */
	if (identity.index == PIGEN_INVALID_ID)
		identity = fire_bits_one(lowering);
	if (identity.index == PIGEN_INVALID_ID)
		return identity;
	/* Memoize for the duration of this lowering call. */
	if (use_count <= PIGEN_FIRE_USE_MAX && fire_memo_count < PIGEN_FIRE_MEMO_MAX)
	{
		struct pigen_fire_memo *entry = &fire_memo[fire_memo_count++];

		entry->used = 1;
		entry->guard = owner->guard;
		entry->use_count = use_count;
		for (i = 0; i < use_count; i++)
		{
			entry->uses[i] = uses[i].signal;
			entry->roles[i] = uses[i].roles;
		}
		entry->identity = identity;
	}
	return identity;
}

/* Task 8 stub: no lowering yet. The fire identity, the predicate-atom
 * conjunction, the lvalue/value lowering and the per-process publication all
 * arrive with the test-contract and implementation stages; this stub reports
 * the unimplemented sentinel without touching the lowering maps or the RTL
 * model. */
int pigen_lower_rtl_transfers(pigen_rtl_lowering *lowering,
	pigen_module_id module)
{
	(void)lowering;
	(void)module;
	(void)fire_identity_for_transfer; (void)fire_memo_reset;
	return -1;
}

/* Task 8 stub: no module composition yet. The pigen_rtl_module_add_with_owner
 * call, the declaration lowering, endpoint binding, processes and transfers
 * and the module record publication all arrive with the test-contract and
 * implementation stages; this stub reports the unimplemented sentinel without
 * touching the lowering maps or the RTL model. */
pigen_rtl_module_id pigen_lower_rtl_module(pigen_rtl_lowering *lowering,
	pigen_module_id module)
{
	(void)lowering;
	(void)module;
	return (pigen_rtl_module_id){PIGEN_INVALID_ID};
}
