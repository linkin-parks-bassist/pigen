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

/* Task 8, pass 1: the shared fire identity and the source-ready equations.
 *
 * Each transfer's canonical guard atoms (lowered through the expression
 * memo, expected-0 atoms negated), its distinct consumer-valid dependencies
 * and its distinct producer-ready dependencies conjoin into ONE RTL
 * expression - the transfer's fire identity. The conjunction is memoized
 * structurally: two transfers whose guard and distinct dependency sets
 * intern to the same canonical key share one lowered expression id, never a
 * re-lowered copy. The one expression is published as the value of the
 * transfer's source-ready equation on the destination payload's input-ready
 * endpoint. The destination updates and the per-process RTL publication are
 * the next pass; this pass deliberately leaves the entry point at the
 * unimplemented -1 sentinel. A failed call reports -1 and the compile stops;
 * it may leave partial state (how/should/compiler/builders/fail.md). */

/* Memoize the 1-bit BITS constant 1 through the expression memo and return
 * its lowered expression identity. The constant is interned as a semantic
 * constant-expression identity keyed by (value 1, 1-bit type), so every
 * zero-operand fire conjunction in the lowering shares one identity and one
 * lowered record (the Task 6 two-valid-constants-share-one-record pattern),
 * and a repeat for an already-lowered identity is a memo hit that publishes
 * nothing new (idempotence). Returns an invalid identity on any failure. */
static pigen_rtl_expr_id fire_bits_one(pigen_rtl_lowering *lowering)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id control_type;
	pigen_const_expr_id interned;
	pigen_rtl_expr_id lowered;
	pigen_source_span origin =
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	logic_one = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (logic_one.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	if (control_type.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	interned = pigen_const_expr_intern_integer(sem, 1, logic_one);
	if (interned.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (lowering->lowered_expression_count > interned.index &&
		lowering->lowered_expressions[interned.index].index !=
		PIGEN_INVALID_ID)
		return lowering->lowered_expressions[interned.index];
	lowered = pigen_rtl_expr_add_bits(rtl, control_type, 1, origin);
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

/* Lower one guard atom into a 1-bit RTL expression: the atom's condition's
 * backing constant lowered through the expression memo, negated (a 1-bit
 * PIGEN_UNARY_LOGICAL_NOT record) when the atom expects 0 and the lowered
 * condition itself when it expects 1. Returns an invalid identity on any
 * failure. */
static pigen_rtl_expr_id fire_lower_atom(pigen_rtl_lowering *lowering,
	const pigen_predicate_atom *atom, pigen_rtl_type_id control_type)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_const_expr_id condition;
	pigen_rtl_expr_id lowered;
	pigen_source_span origin =
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	condition = pigen_expr_constant(sem, atom->condition);
	if (condition.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	lowered = pigen_lower_rtl_expression(lowering, condition);
	if (lowered.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (atom->expected == 1)
		return lowered;
	if (atom->expected != 0)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	{
		pigen_unary_resolution resolution = {
			{PIGEN_CONVERSION_IDENTITY, control_type, control_type},
			{PIGEN_UNARY_LOGICAL_NOT, control_type, control_type}};
		return pigen_rtl_expr_add_unary(rtl, control_type, &resolution,
			lowered, origin);
	}
}

/* Build the transfer's fire identity: the memoized conjunction of its
 * canonical guard atoms (lowered, expected-0 atoms negated) and its distinct
 * consumer-valid and producer-ready dependencies, in that order. The
 * conjunction is one RTL expression id: with zero operands the memoized
 * 1-bit BITS constant 1, with one operand that operand itself, with two or
 * more a left-fold of PIGEN_BINARY_LOGICAL_AND records of the 1-bit
 * unsigned logic type. The conjunction is memoized in the expression memo by
 * a structural canonical key (the same left-fold interned in the semantic
 * constant-expression arena), so two transfers whose guard and distinct
 * dependency sets intern to the same key share ONE lowered expression id -
 * never a re-lowered copy. Returns an invalid identity on any failure. */
static pigen_rtl_expr_id transfer_fire_identity(pigen_rtl_lowering *lowering,
	pigen_transfer_id transfer, const pigen_semantic_transfer *owner)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	const pigen_transfer_signal_use *uses;
	const pigen_predicate *guard;
	const pigen_predicate_atom *atoms;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id control_type;
	pigen_rtl_expr_id *rtl_operands;
	pigen_const_expr_id *key_operands;
	size_t operand_count;
	size_t capacity;
	size_t i;
	pigen_rtl_expr_id key;
	pigen_rtl_expr_id fire;
	pigen_source_span origin =
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};

	if (!owner)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	guard = pigen_predicate_get(sem, owner->guard);
	if (!guard)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	logic_one = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (logic_one.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	if (control_type.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};

	operand_count = 0;
	capacity = 0;
	rtl_operands = NULL;
	key_operands = NULL;
#define FIRE_PUSH_RTL(expr, key) \
	do { \
		if (operand_count == capacity) { \
			capacity = capacity ? capacity * 2 : 4; \
			rtl_operands = pigen_resize(rtl_operands, \
				capacity * sizeof(*rtl_operands)); \
			key_operands = pigen_resize(key_operands, \
				capacity * sizeof(*key_operands)); \
		} \
		if (!rtl_operands || !key_operands) \
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID}; \
		rtl_operands[operand_count] = (expr); \
		key_operands[operand_count] = (key); \
		operand_count++; \
	} while (0)

	/* The canonical guard atoms, in the predicate's arena (canonical) order.
	 * An expected-0 atom is the negation of its lowered condition; an
	 * expected-1 atom is the lowered condition itself. */
	atoms = pigen_predicate_atoms(sem, owner->guard);
	for (i = 0; i < guard->atom_count; i++)
	{
		pigen_rtl_expr_id lowered;
		pigen_unary_operation not_op;
		pigen_const_expr_id condition;
		pigen_const_expr_id key_atom;

		if (!atoms)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		lowered = fire_lower_atom(lowering, &atoms[i], control_type);
		if (lowered.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		condition = pigen_expr_constant(sem, atoms[i].condition);
		if (condition.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		if (atoms[i].expected == 1)
			key_atom = condition;
		else
		{
			not_op = (pigen_unary_operation){
				PIGEN_UNARY_LOGICAL_NOT, logic_one, logic_one};
			key_atom = pigen_const_expr_intern_unary(sem, not_op,
				condition);
			if (key_atom.index == PIGEN_INVALID_ID)
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		}
		FIRE_PUSH_RTL(lowered, key_atom);
	}

	/* The distinct consumer-valid and producer-ready dependencies, in
	 * signal-use order, each (signal, role) pair contributing at most once
	 * (repeated references of one base consume once). A buffered
	 * dependency's endpoint control is driven by its primitive (no
	 * published expression), so it contributes the memoized 1-bit BITS
	 * constant 1 to both the conjunction and the key. */
	if (owner->signal_use_count)
		uses = pigen_transfer_signal_uses(sem, transfer);
	else
		uses = NULL;
	for (i = 0; i < owner->signal_use_count; i++)
	{
		const pigen_transfer_signal_use *use = &uses[i];
		pigen_rtl_expr_id control;
		pigen_const_expr_id key_use;
		int consumer;
		int distinct = 1;
		size_t k;

		if (!uses)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		consumer = (use->roles & PIGEN_TRANSFER_CONSUMER) != 0;
		if (!((use->roles & PIGEN_TRANSFER_CONSUMER) ||
			(use->roles & PIGEN_TRANSFER_PRODUCER)))
			continue;
		for (k = 0; k < i; k++)
			if (uses[k].signal.index == use->signal.index &&
				((uses[k].roles & PIGEN_TRANSFER_CONSUMER) != 0) ==
				consumer)
			{
				distinct = 0;
				break;
			}
		if (!distinct)
			continue;
		if (lowering->lowered_endpoint_count > use->signal.index)
			control = consumer ?
				lowering->lowered_endpoints[use->signal.index].valid :
				lowering->lowered_endpoints[use->signal.index].ready;
		else
			control = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		if (control.index == PIGEN_INVALID_ID)
			control = fire_bits_one(lowering);
		if (control.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		key_use = pigen_const_expr_intern_integer(sem,
			(uint64_t)use->signal.index, logic_one);
		if (key_use.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		FIRE_PUSH_RTL(control, key_use);
	}
#undef FIRE_PUSH_RTL

	if (operand_count == 0)
	{
		key = fire_bits_one(lowering);
		if (key.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return key;
	}

	/* The canonical key: the left-fold of the operand identities interned in
	 * the semantic constant-expression arena (structural, so equal guard and
	 * dependency sets intern to one key). */
	{
		pigen_const_expr_id folded = key_operands[0];
		for (i = 1; i < operand_count; i++)
		{
			pigen_binary_operation and_op = {
				PIGEN_BINARY_LOGICAL_AND, logic_one, logic_one, logic_one};
			folded = pigen_const_expr_intern_binary(sem, and_op, folded,
				key_operands[i]);
			if (folded.index == PIGEN_INVALID_ID)
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		}
		key = folded;
	}

	/* Memo hit: the canonical key was already lowered; return the shared
	 * expression id with no further publication (idempotence). */
	if (lowering->lowered_expression_count > key.index &&
		lowering->lowered_expressions[key.index].index != PIGEN_INVALID_ID)
		return lowering->lowered_expressions[key.index];

	/* One operand is that operand itself; two or more conjoin left-folded as
	 * PIGEN_BINARY_LOGICAL_AND records of the 1-bit unsigned logic type. */
	if (operand_count == 1)
		fire = rtl_operands[0];
	else
	{
		pigen_binary_resolution and_resolution = {
			{PIGEN_CONVERSION_IDENTITY, logic_one, logic_one},
			{PIGEN_CONVERSION_IDENTITY, logic_one, logic_one},
			{PIGEN_BINARY_LOGICAL_AND, logic_one, logic_one, logic_one}};
		fire = rtl_operands[0];
		for (i = 1; i < operand_count; i++)
		{
			fire = pigen_rtl_expr_add_binary(rtl, control_type,
				&and_resolution, fire, rtl_operands[i], origin);
			if (fire.index == PIGEN_INVALID_ID)
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		}
	}
	if (!expr_map_ensure(lowering, key.index + 1) ||
		lowering->lowered_expression_count <= key.index ||
		lowering->lowered_expressions[key.index].index != PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	lowering->lowered_expressions[key.index] = fire;
	return fire;
}

int pigen_lower_rtl_transfers(pigen_rtl_lowering *lowering,
	pigen_module_id module)
{
	pigen_semantic_model *sem;
	pigen_rtl_model *rtl;
	pigen_rtl_module_id rtl_module;
	const pigen_process_id *processes;
	size_t process_count;
	size_t p;

	if (!lowering || !lowering->semantics || !lowering->rtl)
		return -1;
	sem = lowering->semantics;
	rtl = lowering->rtl;

	if (module.index == PIGEN_INVALID_ID ||
		!pigen_module_get(sem, module))
		return -1;

	/* The declaration endpoints must exist before any transfer can resolve
	 * its destination payload and its dependency controls; the call is
	 * idempotent and already implemented. */
	if (pigen_lower_rtl_module_declarations(lowering, module) != 0)
		return -1;

	/* The RTL module that owns this semantic module's objects (resolved by
	 * the declaration pass above, memoized by rtl pointer and semantic
	 * module). */
	rtl_module = rtl_module_for_semantic(rtl, module);
	if (rtl_module.index == PIGEN_INVALID_ID)
		return -1;

	process_count = 0;
	processes = pigen_module_processes(sem, module, &process_count);
	for (p = 0; processes && p < process_count; p++)
	{
		const pigen_transfer_id *transfers;
		size_t transfer_count;
		size_t t;

		transfer_count = 0;
		transfers = pigen_process_transfers(sem, processes[p],
			&transfer_count);
		for (t = 0; transfers && t < transfer_count; t++)
		{
			const pigen_semantic_transfer *owner;
			const pigen_semantic_lvalue *destination;
			pigen_rtl_expr_id fire;
			pigen_rtl_object_id input_ready;

			owner = pigen_transfer_get(sem, transfers[t]);
			if (!owner)
				return -1;
			destination = pigen_lvalue_get(sem, owner->destination);
			if (!destination ||
				destination->kind != PIGEN_LVALUE_PROJECTION ||
				lowering->lowered_endpoint_count <=
					destination->as.projection.signal.index ||
				lowering->lowered_endpoints[
					destination->as.projection.signal.index]
						.input_ready.index == PIGEN_INVALID_ID)
				return -1;
			fire = transfer_fire_identity(lowering, transfers[t], owner);
			if (fire.index == PIGEN_INVALID_ID)
				return -1;
			input_ready = lowering->lowered_endpoints[
				destination->as.projection.signal.index].input_ready;
			/* Publish the fire identity as the value of the transfer's
			 * source-ready equation on the destination payload's
			 * input-ready endpoint, origin the transfer span. */
			if (pigen_rtl_equation_add_with_owner(rtl, rtl_module,
				input_ready, fire, owner->span).index ==
				PIGEN_INVALID_ID)
				return -1;
		}
	}

	/* The destination updates and the per-process RTL publication are the
	 * next pass; this pass deliberately leaves the entry point at the
	 * unimplemented sentinel. */
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
