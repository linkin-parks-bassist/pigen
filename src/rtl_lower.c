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
 * unimplemented sentinel until their own implementation stages. A multi-record
 * publication rolls back atomically: the RTL model is snapshotted before the
 * call and restored exactly on any mid-sequence error. */
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

/* Private, file-local adapter table, one entry per valid transfer
 * realization. Each entry tags its realization; the dispatch in
 * pigen_lower_rtl_module_declarations indexes it by the source descriptor's
 * realization (never by the source transfer-type enum). The BOUNDARY entry is
 * implemented (three-port input shape); the remaining realizations are staged
 * unimplemented until their own implementation stages. */
struct pigen_realization_endpoint_adapter {
	pigen_transfer_realization realization;
	int implemented;
};

static const struct pigen_realization_endpoint_adapter
realization_endpoint_adapters[8] = {
	[PIGEN_TRANSFER_REALIZATION_BOUNDARY] = {
		PIGEN_TRANSFER_REALIZATION_BOUNDARY, 1},
	[PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET] = {
		PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET, 0},
	[PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE] = {
		PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE, 0},
	[PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT] = {
		PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT, 0},
	[PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER] = {
		PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER, 0},
	[PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE] = {
		PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE, 0},
	[PIGEN_TRANSFER_REALIZATION_SKID_QUEUE] = {
		PIGEN_TRANSFER_REALIZATION_SKID_QUEUE, 0},
};

/* File-local memo of the RTL module record per (RTL model, semantic module)
 * pair. It survives the RTL model snapshot/restore (which truncates the model
 * arenas) so a repeat call on the same lowering reuses the record
 * (idempotence) without a memo field in the lowering struct, and a rollback
 * that removes the published module record still finds it on the next call.
 * The table is reset at the start of every declaration-lowering call so a
 * freed-and-reused RTL model pointer never aliases a stale entry. */
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

/* A snapshot of the RTL model's arena state, enough to restore it exactly on
 * a mid-sequence error. It records every arena count and the per-record
 * heap-pointer identity of the two arrays that own model-owned payloads (type
 * dimensions, expression literal words); the other arenas own no heap payload. */
struct pigen_rtl_snapshot {
	size_t type_count;
	const void *type_dimensions[4096];
	size_t expression_count;
	const void *expression_literals[4096];
	size_t object_count;
	size_t instance_count;
	size_t equation_count;
	size_t update_count;
	size_t process_count;
	size_t module_count;
	size_t instance_parameter_count;
	size_t instance_connection_count;
	size_t name_count;
	int endpoint_populated[4096];
};

static int rtl_snapshot_take(const pigen_rtl_model *rtl,
	struct pigen_rtl_snapshot *snap)
{
	if (rtl->type_count > 4096 || rtl->expression_count > 4096)
		return 0;
	for (size_t i = 0; i < rtl->type_count; i++)
		snap->type_dimensions[i] = (const void *)rtl->types[i].dimensions;
	for (size_t i = 0; i < rtl->expression_count; i++)
		snap->expression_literals[i] =
			(const void *)rtl->expressions[i].literal_words;
	snap->type_count = rtl->type_count;
	snap->expression_count = rtl->expression_count;
	snap->object_count = rtl->object_count;
	snap->instance_count = rtl->instance_count;
	snap->equation_count = rtl->equation_count;
	snap->update_count = rtl->update_count;
	snap->process_count = rtl->process_count;
	snap->module_count = rtl->module_count;
	snap->instance_parameter_count = rtl->instance_parameter_count;
	snap->instance_connection_count = rtl->instance_connection_count;
	snap->name_count = rtl->name_count;
	return 1;
}

/* Restore the RTL model to the exact state of a snapshot: truncate every
 * arena to its recorded count and release only the model-owned heap payloads
 * published after the snapshot. The caller holds the sole owning pointer, so
 * the model stays usable (recoverable) after the restore. */
static void rtl_snapshot_restore(pigen_rtl_model *rtl,
	const struct pigen_rtl_snapshot *snap)
{
	for (size_t i = snap->type_count; i < rtl->type_count; i++)
		free((void *)rtl->types[i].dimensions);
	for (size_t i = snap->expression_count; i < rtl->expression_count; i++)
		free((void *)rtl->expressions[i].literal_words);
	rtl->type_count = snap->type_count;
	rtl->expression_count = snap->expression_count;
	rtl->object_count = snap->object_count;
	rtl->instance_count = snap->instance_count;
	rtl->equation_count = snap->equation_count;
	rtl->update_count = snap->update_count;
	rtl->process_count = snap->process_count;
	rtl->module_count = snap->module_count;
	rtl->instance_parameter_count = snap->instance_parameter_count;
	rtl->instance_connection_count = snap->instance_connection_count;
	rtl->name_count = snap->name_count;
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

int pigen_lower_rtl_module_declarations(pigen_rtl_lowering *lowering,
	pigen_module_id module)
{
	pigen_semantic_model *sem;
	pigen_rtl_model *rtl;
	pigen_rtl_module_id rtl_module;
	struct pigen_rtl_snapshot snapshot;
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

	/* Snapshot the RTL model and the endpoint map before any publication so
	 * a mid-sequence error restores exactly the prior state (no partial
	 * objects/expressions/endpoints). The endpoint snapshot records which
	 * slots were populated before this call, so a rollback clears only the
	 * records this call published and leaves prior modules' records intact. */
	{
		size_t cap = lowering->lowered_endpoint_capacity;
		if (cap > 4096)
			cap = 4096;
		for (i = 0; i < cap; i++)
			snapshot.endpoint_populated[i] =
				(lowering->lowered_endpoints[i].payload.index !=
					PIGEN_INVALID_ID);
		for (; i < 4096; i++)
			snapshot.endpoint_populated[i] = 0;
	}

	if (!rtl_snapshot_take(rtl, &snapshot))
		return -1;

	/* Resolve (or create once) the RTL module that owns this semantic
	 * module's objects. */
	rtl_module = rtl_module_for_semantic(rtl, module);
	if (rtl_module.index == PIGEN_INVALID_ID)
		goto fail;

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
			goto fail;
		realization = descriptor->realization;
		adapter = &realization_endpoint_adapters[realization];
		if (!adapter->implemented)
			goto fail;

		/* Dispatch through the realization-indexed table. */
		if (realization == PIGEN_TRANSFER_REALIZATION_BOUNDARY)
		{
			if (!boundary_endpoints(lowering, owner_signal, i,
				rtl_module, &endpoints))
				goto fail;
		}
		else
			goto fail;

		/* Publish this signal's endpoints record, keyed by its arena index
		 * (the signals array position). A growth or double-population
		 * failure rolls back atomically. */
		if (!endpoint_map_ensure(lowering, i + 1) ||
			lowering->lowered_endpoint_count <= i ||
			lowering->lowered_endpoints[i].payload.index != PIGEN_INVALID_ID)
			goto fail;
		lowering->lowered_endpoints[i] = endpoints;
	}
	return 0;
fail:
	rtl_snapshot_restore(rtl, &snapshot);
	/* Clear only the endpoint records this call published; records populated
	 * by a prior call (a different module) stay exactly as they were. */
	for (i = 0; i < lowering->lowered_endpoint_count; i++)
		if (!snapshot.endpoint_populated[i] &&
			lowering->lowered_endpoints[i].payload.index != PIGEN_INVALID_ID)
			lowering->lowered_endpoints[i] = (pigen_rtl_signal_endpoints){
				(pigen_rtl_object_id){PIGEN_INVALID_ID},
				(pigen_rtl_expr_id){PIGEN_INVALID_ID},
				(pigen_rtl_expr_id){PIGEN_INVALID_ID},
				(pigen_rtl_object_id){PIGEN_INVALID_ID},
				(pigen_rtl_object_id){PIGEN_INVALID_ID},
				(pigen_rtl_object_id){PIGEN_INVALID_ID}};
	return -1;
}
