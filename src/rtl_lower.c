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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/rtl_lower.h"
#include "pigen/transfer_type.h"
#include "pigen/util.h"
#include "pigen/predicate.h"
#include "pigen/expression.h"

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
	free(lowering->protocol_ports);
	free(lowering->retained_observation_objects);
	free(lowering->storage_drives);
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
	pigen_packed_dimension *expanded_dimensions = NULL;
	pigen_rtl_packed_dimension *dimensions = NULL;
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

	if (!width) return (pigen_rtl_type_id){PIGEN_INVALID_ID};

	dimension_count = pigen_data_type_dimension_count(sem, type);
	owner_dimensions = pigen_data_type_dimensions(sem, type);
	pigen_sv_integer_keyword keyword = pigen_data_type_sv_keyword(sem,type);
	if ((pigen_data_type_alias_symbol(sem,type).index != PIGEN_INVALID_ID ||
		(keyword != PIGEN_SV_INTEGER_NONE && dimension_count)) &&
		pigen_data_type_is_integral(sem,type))
	{
		if (!pigen_data_type_query_dimensions(sem,type,&expanded_dimensions,&dimension_count))
			return (pigen_rtl_type_id){PIGEN_INVALID_ID};
		/* Scalar integer keywords supply their own atom width. Packed
		 * extensions require a vector type in SV, including that atom range. */
		if (keyword != PIGEN_SV_INTEGER_NONE)
		{
			if (dimension_count == 1)
				dimension_count = 0;
			else if (dimension_count > 1)
				keyword = PIGEN_SV_INTEGER_NONE;
		}
		owner_dimensions = expanded_dimensions;
	}
	if (dimension_count && !owner_dimensions)
	{ free(expanded_dimensions); return (pigen_rtl_type_id){PIGEN_INVALID_ID}; }

	descriptor = (pigen_rtl_type){0};
	descriptor.signedness = signedness;
	descriptor.state_domain = state_domain;
	descriptor.width = width;
	descriptor.sv_keyword = (int)keyword;
	descriptor.width_expression = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	descriptor.dimension_count = dimension_count;

	/* Copy the owner's packed range shape into model-owned bounds. A concrete
	 * bound keeps its evaluated value; a symbolic bound keeps its lowered child
	 * expression (the expression lowering stage publishes that child). */
	if (dimension_count)
	{
		if (dimension_count > SIZE_MAX / sizeof(*dimensions))
		{ free(expanded_dimensions); return (pigen_rtl_type_id){PIGEN_INVALID_ID}; }
		dimensions = pigen_resize(NULL,
			dimension_count * sizeof(*dimensions));
		if (!dimensions)
		{ free(expanded_dimensions); return (pigen_rtl_type_id){PIGEN_INVALID_ID}; }
		for (i = 0; i < dimension_count; i++)
		{
			int64_t bound_value;

			if (pigen_const_expr_evaluate_i64(sem,
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
					free(expanded_dimensions);
					return (pigen_rtl_type_id){PIGEN_INVALID_ID};
				}
				dimensions[i].left = (pigen_rtl_bound){0, child};
			}
			if (pigen_const_expr_evaluate_i64(sem,
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
					free(expanded_dimensions);
					return (pigen_rtl_type_id){PIGEN_INVALID_ID};
				}
				dimensions[i].right = (pigen_rtl_bound){0, child};
			}
		}
		descriptor.dimensions = dimensions;
	}
	free(expanded_dimensions);

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
	/* Computing the type may intern width constants and reallocate the
	 * semantic constant-expression arena; refresh the borrowed owner. */
	owner = pigen_const_expr_get(sem, expression);
	if (!owner) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};

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
		const pigen_integer *integer = &sem->integers[owner->as.exact_integer.index];
		if (integer->negative) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		size_t count = integer->limb_count / 2 + (integer->limb_count % 2 != 0);
		if (!count) count = 1;
		if (count > SIZE_MAX / sizeof(pigen_rtl_literal_word) || count > SIZE_MAX / 64)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		pigen_rtl_literal_word *words = calloc(count, sizeof *words);
		if (!words) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		for (size_t i = 0; i < integer->limb_count; i++)
			words[i / 2].value |= (uint64_t)sem->integer_limbs[integer->first_limb+i] << ((i % 2)*32);
		result = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_INTEGER,
			result_type, words, count*64, 0, origin);
		free(words);
		break;
	}
	case PIGEN_CONST_EXPR_BITS:
	{
		const pigen_bit_state *states = pigen_const_expr_bits(sem, expression);
		size_t count = owner->as.bits.state_count;
		size_t word_count = count / 64 + (count % 64 != 0);
		if (!states || !count || word_count > SIZE_MAX / sizeof(pigen_rtl_literal_word))
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		pigen_rtl_literal_word *words = calloc(word_count, sizeof *words);
		if (!words) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		for (size_t i = 0; i < count; i++)
		{
			uint64_t bit = UINT64_C(1) << (i % 64);
			if (states[i] == PIGEN_BIT_ONE) words[i / 64].value |= bit;
			else if (states[i] == PIGEN_BIT_X) words[i / 64].x_mask |= bit;
			else if (states[i] == PIGEN_BIT_Z) words[i / 64].z_mask |= bit;
		}
		result = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_BITS,
			result_type, words, count, 0, origin);
		free(words);
		break;
	}
	case PIGEN_CONST_EXPR_CONDITIONAL:
	{
		pigen_const_expr snapshot = *owner;
		pigen_conditional_operation operation = snapshot.as.conditional.operation;
		pigen_rtl_expr_id condition = pigen_lower_rtl_expression(lowering, snapshot.as.conditional.condition);
		pigen_rtl_expr_id when_true = pigen_lower_rtl_expression(lowering, snapshot.as.conditional.when_true);
		pigen_rtl_expr_id when_false = pigen_lower_rtl_expression(lowering, snapshot.as.conditional.when_false);
		if (condition.index == PIGEN_INVALID_ID || when_true.index == PIGEN_INVALID_ID ||
			when_false.index == PIGEN_INVALID_ID) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		pigen_conditional_resolution resolution = {
			{PIGEN_CONVERSION_IDENTITY, operation.condition_data_type, operation.condition_data_type},
			{PIGEN_CONVERSION_IDENTITY, operation.when_true_data_type, operation.when_true_data_type},
			{PIGEN_CONVERSION_IDENTITY, operation.when_false_data_type, operation.when_false_data_type},
			operation};
		result = pigen_rtl_expr_add_conditional(rtl, result_type, &resolution,
			condition, when_true, when_false, origin);
		break;
	}
	case PIGEN_CONST_EXPR_UNARY:
	{
		pigen_unary_operation operation = owner->as.unary.operation;
		pigen_rtl_expr_id operand = pigen_lower_rtl_expression(lowering, owner->as.unary.operand);
		if (operand.index == PIGEN_INVALID_ID) return operand;
		pigen_unary_resolution resolution = {
			{PIGEN_CONVERSION_IDENTITY, operation.operand_data_type, operation.operand_data_type},
			operation};
		result = pigen_rtl_expr_add_unary(rtl, result_type, &resolution, operand, origin);
		break;
	}
	case PIGEN_CONST_EXPR_SYMBOL:
	{
		const pigen_symbol *symbol = pigen_symbol_get(sem, owner->as.symbol);
		if (!symbol || symbol->kind != PIGEN_SYMBOL_PARAMETER)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		const pigen_semantic_parameter *parameter = pigen_parameter_get(sem,
			pigen_symbol_parameter(sem, owner->as.symbol));
		const pigen_semantic_expr *value = parameter ? pigen_expr_get(sem, parameter->value) : NULL;
		pigen_conversion conversion;
		if (!value || !pigen_data_type_resolve_assignment_conversion(sem,
			value->data_type, owner->data_type, &conversion))
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		pigen_const_expr_id source = pigen_expr_constant(sem, parameter->value);
		pigen_rtl_expr_id child = pigen_lower_rtl_expression(lowering, source);
		if (child.index == PIGEN_INVALID_ID) return child;
		result = pigen_rtl_expr_add_conversion(rtl, result_type, &conversion, child, origin);
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
		if (owner->as.conversion.conversion.kind==PIGEN_CONVERSION_FOUR_TO_TWO)
		{
			pigen_const_expr_id source_id=owner->as.conversion.operand;
			const pigen_const_expr *source=pigen_const_expr_get(sem,source_id);
			uint64_t source_bits=0;
			int unknown=0;
			if (source && pigen_const_expr_evaluate_u64(sem,
				pigen_data_type_packed_width(sem,source->data_type),&source_bits))
				for (uint64_t bit=0;bit<source_bits;bit++)
				{
					pigen_bit_state state;
					if (!pigen_const_expr_evaluate_bit(sem,source_id,bit,&state)) break;
					if (state>PIGEN_BIT_ONE) {unknown=1;break;}
				}
			/* Clear constant X/Z before RTL publication. Preserve ordinary
			 * known-value conversion records and their source identities. */
			if (unknown)
			{
				const pigen_rtl_type *target=pigen_rtl_type_get(rtl,result_type);
				if (!target || !target->width) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
				uint64_t bits=target->width;
				size_t count=bits/64+(bits%64!=0);
				if (!count || count>SIZE_MAX/sizeof(pigen_rtl_literal_word))
					return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
				pigen_rtl_literal_word *words=calloc(count,sizeof(*words));
				if (!words) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
				int complete=1;
				for (uint64_t bit=0;bit<bits;bit++)
				{
					pigen_bit_state state;
					if (!pigen_const_expr_evaluate_bit(sem,expression,bit,&state) || state>PIGEN_BIT_ONE)
					{complete=0;break;}
					if (state==PIGEN_BIT_ONE) words[bit/64].value|=UINT64_C(1)<<(bit%64);
				}
				if (complete)
				{
					result=pigen_rtl_expr_add_literal(rtl,PIGEN_RTL_EXPR_BITS,result_type,words,bits,0,origin);
					free(words);break;
				}
				free(words);
			}
			owner=pigen_const_expr_get(sem,expression);
			if (!owner) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		}
		const pigen_const_expr *source = pigen_const_expr_get(sem, owner->as.conversion.operand);
		if (source && pigen_data_type_numerical_interpretation(sem, source->data_type) == PIGEN_NUMERICAL_EXACT_INTEGER &&
			pigen_integer_is_negative(sem, pigen_data_type_exact_value(sem, source->data_type)))
		{
			const pigen_integer *integer = &sem->integers[pigen_data_type_exact_value(sem, source->data_type).index];
			const pigen_rtl_type *target = pigen_rtl_type_get(rtl, result_type);
			uint64_t bits = target->width;
			size_t count = bits / 64 + (bits % 64 != 0);
			if (!count || count > SIZE_MAX / sizeof(pigen_rtl_literal_word))
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			pigen_rtl_literal_word *words = calloc(count, sizeof *words);
			if (!words) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			for (size_t i = 0; i < integer->limb_count && i / 2 < count; i++)
				words[i / 2].value |= (uint64_t)sem->integer_limbs[integer->first_limb+i] << ((i % 2)*32);
			uint64_t carry = 1;
			for (size_t i = 0; i < count; i++)
			{
				words[i].value = ~words[i].value + carry;
				carry = carry && words[i].value == 0;
			}
			if (bits % 64) words[count-1].value &= (UINT64_C(1) << (bits % 64))-1;
			result = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_BITS,
				result_type, words, bits, 0, origin);
			free(words);
			break;
		}

		operand = pigen_lower_rtl_expression(lowering,
			owner->as.conversion.operand);
		if (operand.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		owner = pigen_const_expr_get(sem, expression);
		if (!owner) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
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
		{
			pigen_select_kind kind = owner->as.select.kind;

			owner = pigen_const_expr_get(sem, expression);
			if (!owner) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			result = pigen_rtl_expr_add_select(rtl, result_type, base,
				left, right, kind, origin);
		}
		break;
	}
	case PIGEN_CONST_EXPR_CONCATENATION:
	{
		const pigen_const_expr_id *owner_children;
		pigen_const_expr_id *ids;
		size_t child_count;
		size_t i;
		pigen_rtl_expr_id *children;

		child_count = owner->as.sequence.child_count;
		owner_children = pigen_const_expr_children(sem,
			owner->as.sequence.first_child, child_count);
		if (child_count && !owner_children)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		ids = pigen_resize(NULL, child_count * sizeof(*ids));
		if (child_count && !ids) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		if (child_count)
			memcpy(ids, owner_children, child_count * sizeof(*ids));
		if (child_count > SIZE_MAX / sizeof(*children))
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		children = pigen_resize(NULL,
			child_count * sizeof(*children));
		if (!children && child_count)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		for (i = 0; i < child_count; i++)
		{
			children[i] = pigen_lower_rtl_expression(lowering, ids[i]);
			if (children[i].index == PIGEN_INVALID_ID)
			{
				free(children);
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			}
		}
		result = pigen_rtl_expr_add_concatenation(rtl, result_type,
			children, child_count, origin);
		free(children);
		free(ids);
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
	pigen_semantic_model *sem, pigen_module_id semantic)
{
	for (size_t i = 0; i < pigen_module_memo_count; i++)
		if (pigen_module_memo[i].rtl == rtl &&
			pigen_module_memo[i].semantic.index == semantic.index)
			return pigen_module_memo[i].module;
	/* The arena is authoritative after a module memo reset, including revisits
	 * after another module was lowered. Never manufacture a second owner. */
	for(size_t i=0;i<rtl->module_count;i++)
		if(rtl->modules[i].semantic_module.index==semantic.index) return (pigen_rtl_module_id){(uint32_t)i};
	{
		pigen_rtl_module_id created = pigen_rtl_module_add_with_owner(rtl,
			semantic,
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
		if (created.index == PIGEN_INVALID_ID)
			return created;
		{
			const pigen_semantic_module *module =
				pigen_module_get(sem, semantic);
			const pigen_symbol *symbol = module ?
				pigen_symbol_get(sem, module->symbol) : NULL;
			size_t length = 0;
			const char *text = symbol ? pigen_source_span_text(
				sem->sources, symbol->name, &length) : NULL;

			if (text)
			{
				char *name = pigen_resize(NULL, length + 1);

				memcpy(name, text, length);
				name[length] = 0;
				pigen_rtl_set_name(rtl,
					&rtl->modules[created.index].name, name);
				free(name);
			}
		}
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
/* ---- Native lowering helpers --------------------------------------------
 *
 * Every published object carries its final terminal name. Source payloads use
 * the declared identifier spelling; generated controls use deterministic
 * suffixes. The names are model-owned and the emitter prefers them, so
 * synthetic wires and primitive instances need no separate name request.
 */
static char *name_join(const char *base, const char *suffix)
{
	size_t base_length = strlen(base);
	size_t suffix_length = strlen(suffix);
	char *name = pigen_resize(NULL, base_length + suffix_length + 1);

	memcpy(name, base, base_length);
	memcpy(name + base_length, suffix, suffix_length);
	name[base_length + suffix_length] = 0;
	return name;
}

static char *scoped_symbol_spelling(const pigen_semantic_model *sem,const pigen_symbol *symbol)
{
	size_t length=0;
	const char *text=symbol ? pigen_source_span_text(sem->sources,symbol->name,&length) : NULL;
	if(!text) return NULL;
	int generated=0;
	for(pigen_scope_id scope=symbol->scope;pigen_scope_get(sem,scope);scope=sem->scopes[scope.index].parent)
	{
		if (sem->scopes[scope.index].is_procedural) generated=1;
		for(size_t i=0;i<sem->generate_scope_count;i++)
			if(sem->generate_scopes[i].scope.index==scope.index) generated=1;
	}
	pigen_string name={0};
	if(generated) pigen_append_format(&name,"__pigen_scope_%u_",symbol->scope.index);
	pigen_append_range(&name,text,length);
	return name.data;
}

static char *signal_spelling(pigen_rtl_lowering *lowering,
	const pigen_semantic_signal *signal)
{
	pigen_protocol_channel channel;
	pigen_protocol_endpoint_id native_id = pigen_protocol_endpoint_for_signal(lowering->semantics,
		pigen_symbol_signal(lowering->semantics, signal->symbol), &channel);
	const pigen_semantic_protocol_endpoint *native = pigen_protocol_endpoint_get(lowering->semantics, native_id);
	if (native && !native->is_instance_view)
	{
		const pigen_symbol *owner = pigen_symbol_get(lowering->semantics, native->symbol);
		char *base=scoped_symbol_spelling(lowering->semantics,owner);
		if(!base) return NULL;
		pigen_string name = {0}; pigen_append(&name,base); free(base);
		pigen_append(&name, "__pigen_channel_"); pigen_append(&name, pigen_protocol_channel_name(channel));
		return name.data;
	}
	const pigen_symbol *symbol = pigen_symbol_get(lowering->semantics,signal->symbol);
	return scoped_symbol_spelling(lowering->semantics,symbol);
}

static int name_object(pigen_rtl_lowering *lowering,
	pigen_rtl_object_id object, char *name)
{
	if (!name)
		return 0;
	if (!pigen_rtl_object_get(lowering->rtl, object))
	{
		free(name);
		return 0;
	}
	pigen_rtl_set_name(lowering->rtl,
		&lowering->rtl->objects[object.index].name, name);
	free(name);
	return 1;
}

static char *signal_shape_suffix(pigen_rtl_lowering *lowering,
	size_t signal_index)
{
	pigen_semantic_model *sem = lowering->semantics;
	const pigen_semantic_signal *signal = pigen_signal_get(sem,
		(pigen_signal_id){(uint32_t)signal_index});
	const pigen_semantic_shape *shape;
	const pigen_shape_dimension *dimensions;
	pigen_string text = {0};
	size_t i;

	if (!signal || signal->shape.index == PIGEN_INVALID_ID)
		return NULL;
	shape = pigen_shape_get(sem, signal->shape);
	dimensions = shape ? pigen_shape_dimensions(sem, signal->shape) : NULL;
	if (!shape || !shape->dimension_count || !dimensions)
		return NULL;
	for (i = 0; i < shape->dimension_count; i++)
	{
		uint64_t left = 0, right = 0;

		if (dimensions[i].form == PIGEN_SHAPE_DIMENSION_RANGE)
		{
			int64_t signed_left,signed_right;
			if (pigen_const_expr_evaluate_i64(sem,dimensions[i].as.range.left,&signed_left) &&
				pigen_const_expr_evaluate_i64(sem,dimensions[i].as.range.right,&signed_right))
			{
				pigen_append_format(&text," [%lld:%lld]",(long long)signed_left,(long long)signed_right);
				continue;
			}
			if (!pigen_const_expr_evaluate_u64(sem,
					dimensions[i].as.range.left, &left) ||
				!pigen_const_expr_evaluate_u64(sem,
					dimensions[i].as.range.right, &right))
			{
				free(text.data);
				return NULL;
			}
		}
		else
		{
			if (!pigen_const_expr_evaluate_u64(sem,
					dimensions[i].as.count, &left) || left == 0)
			{
				free(text.data);
				return NULL;
			}
			right = 0;
			left -= 1;
		}
		pigen_append_format(&text, " [%llu:%llu]",
			(unsigned long long)left, (unsigned long long)right);
	}
	return text.data;
}

static int signal_is_child_endpoint(const pigen_semantic_model *model, size_t index)
{
	return pigen_signal_is_instance_endpoint(model, (pigen_signal_id){(uint32_t)index});
}

static pigen_symbol_id static_record_owner(const pigen_semantic_model *sem,
	pigen_data_type_id type)
{
	for (size_t depth = 0; depth <= sem->data_type_count; depth++)
	{
		pigen_symbol_id owner = pigen_data_type_alias_symbol(sem, type);
		if (owner.index == PIGEN_INVALID_ID) break;
		for (size_t i = 0; i < sem->struct_member_count; i++)
			if (sem->struct_members[i].owner.index == owner.index) return owner;
		type = pigen_data_type_alias_target(sem, type);
	}
	return (pigen_symbol_id){PIGEN_INVALID_ID};
}

static int append_static_dimensions(pigen_string *text, pigen_semantic_model *sem,
	const pigen_packed_dimension *dimensions, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		int64_t left, right;
		if (!pigen_const_expr_evaluate_i64(sem, dimensions[i].left, &left) ||
			!pigen_const_expr_evaluate_i64(sem, dimensions[i].right, &right)) return 0;
		pigen_append_format(text, " [%lld:%lld]", (long long)left, (long long)right);
	}
	return 1;
}

/* Prepare declarations above the semantic-free terminal boundary. Inline
 * schemas avoid depending on authored typedef visibility or generated names. */
static int append_static_data_type(pigen_string *text, pigen_semantic_model *sem,
	pigen_data_type_id type, size_t depth)
{
	if (depth > sem->data_type_count) return 0;
	pigen_symbol_id owner = static_record_owner(sem, type);
	if (owner.index == PIGEN_INVALID_ID)
	{
		pigen_append(text, pigen_data_type_state_domain(sem,type) == PIGEN_DATA_TYPE_STATE_TWO ? "bit" : "logic");
		if (pigen_data_type_signedness(sem,type) == PIGEN_SIGN_SIGNED) pigen_append(text," signed");
		pigen_packed_dimension *dimensions = NULL; size_t count = 0;
		if (!pigen_data_type_query_dimensions(sem,type,&dimensions,&count)) return 0;
		int ok = append_static_dimensions(text,sem,dimensions,count);
		free(dimensions); return ok;
	}
	pigen_append(text, pigen_data_type_signedness(sem,type) == PIGEN_SIGN_SIGNED ?
		"struct packed signed { " : "struct packed { ");
	size_t count = 0;
	for (size_t i = 0; i < sem->struct_member_count; i++)
		if (sem->struct_members[i].owner.index == owner.index) count++;
	pigen_semantic_struct_member *members = pigen_resize(NULL,count*sizeof(*members));
	size_t at = 0;
	for (size_t i = 0; i < sem->struct_member_count; i++)
		if (sem->struct_members[i].owner.index == owner.index) members[at++] = sem->struct_members[i];
	for (size_t i = 1; i < count; i++)
	{
		pigen_semantic_struct_member member = members[i]; size_t j = i;
		while (j && members[j-1].offset < member.offset) { members[j] = members[j-1]; j--; }
		members[j] = member;
	}
	for (size_t i = 0; i < count; i++)
	{
		size_t length = 0;
		const char *name = pigen_source_span_text(sem->sources,members[i].name,&length);
		if (!name || !length || !append_static_data_type(text,sem,members[i].data_type,depth+1))
		{ free(members); return 0; }
		pigen_append(text," "); pigen_append_range(text,name,length); pigen_append(text," ; ");
	}
	free(members); pigen_append(text,"}");
	/* Preserve packed dimensions outside the schema, not the flat vector
 * used internally to calculate the record's width. */
	for (size_t n = 0; n <= sem->data_type_count; n++)
	{
		if (!append_static_dimensions(text,sem,pigen_data_type_dimensions(sem,type),
			pigen_data_type_dimension_count(sem,type))) return 0;
		if (pigen_data_type_alias_symbol(sem,type).index == owner.index) return 1;
		type = pigen_data_type_alias_target(sem,type);
	}
	return 0;
}

static pigen_rtl_object_id add_named_object(pigen_rtl_lowering *lowering,
	pigen_rtl_module_id rtl_module, pigen_rtl_type_id type,
	pigen_semantic_direction direction, size_t signal_index,
	pigen_source_span origin, const char *base, const char *suffix)
{
	pigen_rtl_model *rtl = lowering->rtl;
	if (signal_is_child_endpoint(lowering->semantics, signal_index))
		direction = PIGEN_SEMANTIC_INTERNAL;
	pigen_rtl_object_id object = pigen_rtl_object_add_with_owner(rtl,
		rtl_module, PIGEN_RTL_OBJECT_VARIABLE, type, direction,
		(pigen_signal_id){(uint32_t)signal_index}, origin);
	char *name;

	if (object.index == PIGEN_INVALID_ID)
		return object;
	name = suffix ? name_join(base, suffix) : name_join(base, "");
	if (!name || !name_object(lowering, object, name))
		return (pigen_rtl_object_id){PIGEN_INVALID_ID};
	if (!suffix)
	{
		const pigen_semantic_signal *signal = pigen_signal_get(
			lowering->semantics, (pigen_signal_id){(uint32_t)signal_index});
		const pigen_transfer_type_descriptor *descriptor = signal ?
			pigen_transfer_type_descriptor_get(signal->transfer_type) : NULL;

		if (descriptor && descriptor->is_static)
		{
			if (static_record_owner(lowering->semantics,signal->data_type).index != PIGEN_INVALID_ID)
			{
				pigen_string text = {0};
				if (!append_static_data_type(&text,lowering->semantics,signal->data_type,0))
				{ free(text.data); return (pigen_rtl_object_id){PIGEN_INVALID_ID}; }
				rtl->objects[object.index].declaration_type = text.data;
			}
			char *shape = signal_shape_suffix(lowering, signal_index);
			const pigen_semantic_shape *declared=pigen_shape_get(lowering->semantics,signal->shape);
			if(!shape && declared && declared->dimension_count) return (pigen_rtl_object_id){PIGEN_INVALID_ID};
			if (shape)
			{
				if (!pigen_rtl_object_set_shape(rtl, object, shape))
				{
					free(shape);
					return (pigen_rtl_object_id){PIGEN_INVALID_ID};
				}
				free(shape);
			}
		}
	}
	return object;
}

static pigen_rtl_expr_id object_expression(pigen_rtl_lowering *lowering,
	pigen_rtl_type_id type, pigen_rtl_object_id object, pigen_source_span origin)
{
	return pigen_rtl_expr_add_object(lowering->rtl, type, object, origin);
}

/* One-bit literal expression for a primitive tie-off. */
static pigen_rtl_expr_id control_literal(pigen_rtl_lowering *lowering,
	pigen_rtl_type_id control_type, uint64_t value, pigen_source_span origin)
{
	return pigen_rtl_expr_add_integer(lowering->rtl, control_type, value,
		origin);
}

/* Find a module input signal by its declared spelling (clock and reset
 * convention for the storage primitives). Returns its signal arena index or
 * SIZE_MAX. */
static size_t module_input_named(pigen_rtl_lowering *lowering,
	pigen_module_id module, const char *name)
{
	pigen_semantic_model *sem = lowering->semantics;
	size_t i;

	for (i = 0; i < sem->signal_count; i++)
	{
		const pigen_semantic_signal *signal = &sem->signals[i];
		const pigen_symbol *symbol;
		const char *text;
		size_t length = 0;

		if (signal->module.index != module.index ||
			signal->direction != PIGEN_SEMANTIC_INPUT)
			continue;
		symbol = pigen_symbol_get(sem, signal->symbol);
		if (!symbol)
			continue;
		text = pigen_source_span_text(sem->sources, symbol->name, &length);
		if (text && strlen(name) == length && !memcmp(text, name, length))
			return i;
	}
	return SIZE_MAX;
}

/* A concrete buffered module input is an upstream-owned boundary view: the
 * module exposes payload, valid and ready ports and owns no storage. */
/* Whether a resolved connection expression reads a particular signal symbol.
 * Instance connections are emitted textually today, so this use marker only
 * prevents the unused-input ready tie from conflicting with the child drive. */
static int expr_references_symbol(const pigen_semantic_model *model,
	pigen_expr_id expression, pigen_symbol_id symbol)
{
	const pigen_semantic_expr *expr;

	if (expression.index == PIGEN_INVALID_ID) return 0;
	expr = pigen_expr_get(model, expression);
	if (!expr) return 0;
	switch (expr->kind)
	{
	case PIGEN_EXPR_SYMBOL:
		return expr->as.symbol.index == symbol.index;
	case PIGEN_EXPR_GROUP:
		return expr_references_symbol(model, expr->as.group.operand, symbol);
	case PIGEN_EXPR_UNARY:
		return expr_references_symbol(model, expr->as.unary.operand, symbol);
	case PIGEN_EXPR_BINARY:
		return expr_references_symbol(model, expr->as.binary.left, symbol) ||
			expr_references_symbol(model, expr->as.binary.right, symbol);
	case PIGEN_EXPR_CONDITIONAL:
		return expr_references_symbol(model, expr->as.conditional.condition,
			symbol) ||
			expr_references_symbol(model, expr->as.conditional.when_true,
				symbol) ||
			expr_references_symbol(model, expr->as.conditional.when_false,
				symbol);
	case PIGEN_EXPR_CONVERSION:
		return expr_references_symbol(model, expr->as.conversion.operand,
			symbol);
	case PIGEN_EXPR_INDEX:
		return expr_references_symbol(model, expr->as.index.base, symbol) ||
			expr_references_symbol(model, expr->as.index.index, symbol);
	case PIGEN_EXPR_SELECT:
		return expr_references_symbol(model, expr->as.select.base, symbol) ||
			expr_references_symbol(model, expr->as.select.left, symbol) ||
			expr_references_symbol(model, expr->as.select.right, symbol);
	case PIGEN_EXPR_EVALUATED_VALUE:
		return expr_references_symbol(model,expr->as.evaluation.value,symbol) ||
			expr_references_symbol(model,expr->as.evaluation.evaluated,symbol);
	case PIGEN_EXPR_ARRAY_PATTERN:
	case PIGEN_EXPR_CONCATENATION:
	{
		const pigen_expr_id *children = pigen_expr_children(model,
			expr->as.sequence.first_child, expr->as.sequence.child_count);

		for (size_t i = 0; children && i < expr->as.sequence.child_count; i++)
			if (expr_references_symbol(model, children[i], symbol)) return 1;
		return 0;
	}
	case PIGEN_EXPR_PEEK:
		return expr_references_symbol(model, expr->as.peek.source, symbol);
	default:
		return 0;
	}
}

static int signal_is_instance_destination(const pigen_semantic_model *model,
	pigen_symbol_id symbol)
{
	for (size_t i = 0; i < model->instance_connection_count; i++)
		if (model->instance_connections[i].direction == PIGEN_SEMANTIC_OUTPUT &&
			expr_references_symbol(model, model->instance_connections[i].value,
				symbol))
			return 1;
	return 0;
}

static int boundary_endpoints(pigen_rtl_lowering *lowering,
	const pigen_semantic_signal *owner_signal, size_t signal_index,
	pigen_rtl_module_id rtl_module, pigen_rtl_signal_endpoints *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id data_type;
	pigen_rtl_type_id control_type;
	pigen_rtl_object_id payload;
	pigen_rtl_object_id valid_object;
	pigen_rtl_object_id ready_object;
	pigen_rtl_expr_id valid;
	pigen_rtl_expr_id ready;
	const pigen_symbol *owner_symbol =
		pigen_symbol_get(sem, owner_signal->symbol);
	pigen_source_span origin = owner_symbol ? owner_symbol->name
		: (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
	char *base = signal_spelling(lowering, owner_signal);

	if (!base)
		return 0;
	data_type = pigen_lower_rtl_type(lowering, owner_signal->data_type);
	if (data_type.index == PIGEN_INVALID_ID)
	{
		free(base);
		return 0;
	}
	logic_one = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (logic_one.index == PIGEN_INVALID_ID)
	{
		free(base);
		return 0;
	}
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	if (control_type.index == PIGEN_INVALID_ID)
	{
		free(base);
		return 0;
	}

	payload = add_named_object(lowering, rtl_module, data_type,
		owner_signal->direction, signal_index, origin, base, NULL);
	valid_object = add_named_object(lowering, rtl_module, control_type,
		owner_signal->direction == PIGEN_SEMANTIC_OUTPUT ?
			PIGEN_SEMANTIC_OUTPUT : PIGEN_SEMANTIC_INPUT,
		signal_index, origin, base, "_valid");
	ready_object = add_named_object(lowering, rtl_module, control_type,
		owner_signal->direction == PIGEN_SEMANTIC_OUTPUT ?
			PIGEN_SEMANTIC_INPUT : PIGEN_SEMANTIC_OUTPUT,
		signal_index, origin, base, "_ready");
	free(base);
	if (payload.index == PIGEN_INVALID_ID ||
		valid_object.index == PIGEN_INVALID_ID ||
		ready_object.index == PIGEN_INVALID_ID)
		return 0;
	valid = object_expression(lowering, control_type, valid_object, origin);
	ready = object_expression(lowering, control_type, ready_object, origin);
	if (valid.index == PIGEN_INVALID_ID || ready.index == PIGEN_INVALID_ID)
		return 0;

	*out = (pigen_rtl_signal_endpoints){
		payload, valid, ready, payload, valid_object, ready_object,
		ready_object, (pigen_rtl_object_id){PIGEN_INVALID_ID},
		(pigen_rtl_object_id){PIGEN_INVALID_ID},
		(pigen_rtl_object_id){PIGEN_INVALID_ID},
		(pigen_rtl_object_id){PIGEN_INVALID_ID}};
	return 1;
}

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
	interned = pigen_const_expr_intern_integer(sem, (uint64_t)constant,
		logic_one);
	if (interned.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (lowering->lowered_expression_count > interned.index &&
		lowering->lowered_expressions[interned.index].index !=
		PIGEN_INVALID_ID)
		return lowering->lowered_expressions[interned.index];
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

static int net_variable_endpoints(pigen_rtl_lowering *lowering,
	const pigen_semantic_signal *owner_signal, size_t signal_index,
	pigen_rtl_module_id rtl_module, int valid_constant, int ready_constant,
	pigen_rtl_signal_endpoints *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_type_id data_type;
	pigen_rtl_object_id payload;
	pigen_rtl_expr_id valid;
	pigen_rtl_expr_id ready;
	const pigen_symbol *owner_symbol =
		pigen_symbol_get(sem, owner_signal->symbol);
	pigen_source_span origin = owner_symbol ? owner_symbol->name
		: (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
	char *base = signal_spelling(lowering, owner_signal);

	if (!base)
		return 0;
	data_type = pigen_lower_rtl_type(lowering, owner_signal->data_type);
	if (data_type.index == PIGEN_INVALID_ID)
	{
		free(base);
		return 0;
	}
	payload = add_named_object(lowering, rtl_module, data_type,
		owner_signal->direction, signal_index, origin, base, NULL);
	free(base);
	if (payload.index == PIGEN_INVALID_ID)
		return 0;
	valid = control_constant(lowering, valid_constant);
	if (valid.index == PIGEN_INVALID_ID)
		return 0;
	ready = control_constant(lowering, ready_constant);
	if (ready.index == PIGEN_INVALID_ID)
		return 0;

	*out = (pigen_rtl_signal_endpoints){
		payload, valid, ready, payload, payload, payload,
		(pigen_rtl_object_id){PIGEN_INVALID_ID},
		(pigen_rtl_object_id){PIGEN_INVALID_ID},
		(pigen_rtl_object_id){PIGEN_INVALID_ID},
		(pigen_rtl_object_id){PIGEN_INVALID_ID},
		(pigen_rtl_object_id){PIGEN_INVALID_ID}};
	return 1;
}

static pigen_rtl_primitive primitive_for_realization(
	pigen_transfer_realization realization)
{
	switch (realization)
	{
	case PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT:
		return PIGEN_RTL_PRIMITIVE_BUF;
	case PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER:
		return PIGEN_RTL_PRIMITIVE_PORT;
	case PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE:
		return PIGEN_RTL_PRIMITIVE_FIFO;
	case PIGEN_TRANSFER_REALIZATION_SKID_QUEUE:
		return PIGEN_RTL_PRIMITIVE_SKID;
	default:
		return PIGEN_RTL_PRIMITIVE_INVALID;
	}
}

static const char *primitive_stem(pigen_rtl_primitive primitive)
{
	switch (primitive)
	{
	case PIGEN_RTL_PRIMITIVE_BUF: return "buf";
	case PIGEN_RTL_PRIMITIVE_PORT: return "port";
	case PIGEN_RTL_PRIMITIVE_FIFO: return "fifo";
	case PIGEN_RTL_PRIMITIVE_SKID: return "skid";
	default: return NULL;
	}
}

/* Storage realization: one payload object plus one primitive instance.
 * Internal signals expose internal out_ready; output signals expose the
 * module's ready port directly. The input side (packet_in/in_valid/in_ready)
 * is what a destination transfer writes; the output side (payload/valid) is
 * what a consuming expression reads. */
static int storage_endpoints(pigen_rtl_lowering *lowering,
	const pigen_semantic_signal *owner_signal, size_t signal_index,
	pigen_rtl_module_id rtl_module, pigen_module_id semantic_module,
	pigen_transfer_realization realization, pigen_rtl_signal_endpoints *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_data_type_id logic_one;
	pigen_rtl_type_id data_type;
	pigen_rtl_type_id control_type;
	pigen_rtl_object_id payload;
	pigen_rtl_object_id valid_object;
	pigen_rtl_object_id in_valid;
	pigen_rtl_object_id in_ready;
	pigen_rtl_object_id packet_in;
	pigen_rtl_object_id source_ready;
	pigen_rtl_object_id force_valid_object = {PIGEN_INVALID_ID};
	pigen_rtl_object_id force_invalid_object = {PIGEN_INVALID_ID};
	pigen_rtl_object_id clear_object = {PIGEN_INVALID_ID};
	pigen_rtl_object_id force_after_object = {PIGEN_INVALID_ID};
	pigen_rtl_expr_id valid;
	pigen_rtl_expr_id ready;
	pigen_rtl_expr_id depth = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	pigen_rtl_expr_id *parameter = NULL;
	size_t parameter_count = 0;
	pigen_rtl_primitive primitive;
	pigen_rtl_instance_id instance;
	pigen_rtl_connection connections[13];
	size_t connection_count = 0;
	pigen_rtl_record_range connection_range;
	const pigen_symbol *owner_symbol =
		pigen_symbol_get(sem, owner_signal->symbol);
	pigen_source_span origin = owner_symbol ? owner_symbol->name
		: (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
	char *base = signal_spelling(lowering, owner_signal);
	pigen_semantic_direction payload_direction = owner_signal->direction;
	pigen_semantic_direction control_direction =
		owner_signal->direction == PIGEN_SEMANTIC_OUTPUT ?
		PIGEN_SEMANTIC_OUTPUT : PIGEN_SEMANTIC_INTERNAL;
	size_t clock_index;
	size_t reset_index;
	pigen_rtl_object_id clock_object;
	pigen_rtl_object_id reset_object = (pigen_rtl_object_id){PIGEN_INVALID_ID};
	pigen_rtl_expr_id native_reset = {PIGEN_INVALID_ID};
	pigen_rtl_expr_id zero;
	char *instance_name;
	int ok = 0;

	primitive = primitive_for_realization(realization);
	if (primitive == PIGEN_RTL_PRIMITIVE_INVALID || !base)
		goto done;
	data_type = pigen_lower_rtl_type(lowering, owner_signal->data_type);
	if (data_type.index == PIGEN_INVALID_ID)
		goto done;
	logic_one = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (logic_one.index == PIGEN_INVALID_ID)
		goto done;
	control_type = pigen_lower_rtl_type(lowering, logic_one);
	if (control_type.index == PIGEN_INVALID_ID)
		goto done;

	/* Native protocol bindings own clock/reset identities. Ordinary storage
	 * retains the module's existing clk/reset convention. */
	const pigen_semantic_protocol_endpoint *protocol = pigen_protocol_endpoint_get(sem,
		pigen_protocol_endpoint_for_signal(sem, (pigen_signal_id){(uint32_t)signal_index}, NULL));
	if (protocol && !protocol->binding.is_bound) goto done;
	const pigen_semantic_clock_domain *protocol_domain = protocol ?
		pigen_clock_domain_get(sem, protocol->binding.domain) : NULL;
	clock_index = protocol_domain ? pigen_symbol_signal(sem, protocol_domain->clock_symbol).index :
		module_input_named(lowering, semantic_module, "clk");
	clock_object = (pigen_rtl_object_id){PIGEN_INVALID_ID};
	if (clock_index != SIZE_MAX &&
		lowering->lowered_endpoint_count > clock_index)
		clock_object = lowering->lowered_endpoints[clock_index].payload;
	reset_index = protocol ? pigen_symbol_signal(sem, protocol->binding.reset_symbol).index :
		module_input_named(lowering, semantic_module, "reset");
	if (reset_index != SIZE_MAX &&
		lowering->lowered_endpoint_count > reset_index)
		reset_object = lowering->lowered_endpoints[reset_index].payload;
	if (protocol)
	{
		const pigen_rtl_object *reset_pin = pigen_rtl_object_get(rtl, reset_object);
		if (!reset_pin || clock_object.index == PIGEN_INVALID_ID) goto done;
		native_reset = object_expression(lowering, reset_pin->type, reset_object, origin);
		if (protocol->binding.reset_active_low)
		{
			pigen_unary_resolution inverse;
			if (!pigen_data_type_resolve_unary_operation(sem, PIGEN_UNARY_LOGICAL_NOT,
				sem->signals[reset_index].data_type, &inverse)) goto done;
			pigen_rtl_type_id reset_type = pigen_lower_rtl_type(lowering, inverse.operation.result_data_type);
			native_reset = pigen_rtl_expr_add_unary(rtl, reset_type, &inverse, native_reset, origin);
		}
		if (native_reset.index == PIGEN_INVALID_ID) goto done;
	}

	payload = add_named_object(lowering, rtl_module, data_type,
		payload_direction, signal_index, origin, base, NULL);
	valid_object = add_named_object(lowering, rtl_module, control_type,
		control_direction, signal_index, origin, base,
		owner_signal->direction == PIGEN_SEMANTIC_OUTPUT ?
			"_valid" : "__pigen_valid");
	in_valid = add_named_object(lowering, rtl_module, control_type,
		PIGEN_SEMANTIC_INTERNAL, signal_index, origin, base,
		"__pigen_in_valid");
	in_ready = add_named_object(lowering, rtl_module, control_type,
		PIGEN_SEMANTIC_INTERNAL, signal_index, origin, base,
		"__pigen_in_ready");
	packet_in = add_named_object(lowering, rtl_module, data_type,
		PIGEN_SEMANTIC_INTERNAL, signal_index, origin, base,
		"__pigen_packet_in");
	if (owner_signal->direction == PIGEN_SEMANTIC_OUTPUT)
		source_ready = add_named_object(lowering, rtl_module, control_type,
			PIGEN_SEMANTIC_INPUT, signal_index, origin, base, "_ready");
	else
		source_ready = add_named_object(lowering, rtl_module, control_type,
			PIGEN_SEMANTIC_INTERNAL, signal_index, origin, base,
			"__pigen_out_ready");
	if (payload.index == PIGEN_INVALID_ID ||
		valid_object.index == PIGEN_INVALID_ID ||
		in_valid.index == PIGEN_INVALID_ID ||
		in_ready.index == PIGEN_INVALID_ID ||
		packet_in.index == PIGEN_INVALID_ID ||
		source_ready.index == PIGEN_INVALID_ID)
		goto done;
	{
		force_valid_object = add_named_object(lowering,
			rtl_module, control_type, PIGEN_SEMANTIC_INTERNAL, signal_index,
			origin, base, "__pigen_force_valid");
		force_invalid_object = add_named_object(lowering,
			rtl_module, control_type, PIGEN_SEMANTIC_INTERNAL, signal_index,
			origin, base, "__pigen_force_invalid");
		clear_object = add_named_object(lowering, rtl_module,
			control_type, PIGEN_SEMANTIC_INTERNAL, signal_index, origin, base,
			"__pigen_clear");
		force_after_object = add_named_object(lowering,
			rtl_module, control_type, PIGEN_SEMANTIC_INTERNAL, signal_index,
			origin, base, "__pigen_force_after_transfer");

		if (force_valid_object.index == PIGEN_INVALID_ID ||
			force_invalid_object.index == PIGEN_INVALID_ID ||
			clear_object.index == PIGEN_INVALID_ID ||
			force_after_object.index == PIGEN_INVALID_ID)
			goto done;
		/* published below with the endpoints record */
	}
	valid = object_expression(lowering, control_type, valid_object, origin);
	ready = object_expression(lowering, control_type, in_ready, origin);
	if (valid.index == PIGEN_INVALID_ID || ready.index == PIGEN_INVALID_ID)
		goto done;

	zero = control_constant(lowering, 0);
	if (zero.index == PIGEN_INVALID_ID)
		goto done;

	if (realization == PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE)
	{
		pigen_const_expr_id argument_const;

		if (owner_signal->transfer_argument.index == PIGEN_INVALID_ID)
			goto done;
		argument_const = pigen_expr_constant(lowering->semantics,
			owner_signal->transfer_argument);
		if (argument_const.index == PIGEN_INVALID_ID)
			goto done;
		/* The storage definition declares DEPTH as a signed 32-bit int.
		 * Finish that interface conversion here, independent of the width
		 * of a genvar-derived constant expression. */
		uint64_t count=0;int64_t signed_count=0;
		if(pigen_const_expr_evaluate_i64(sem,argument_const,&signed_count))
		{
			if(signed_count<=0) goto done;
			count=(uint64_t)signed_count;
		}
		else if(!pigen_const_expr_evaluate_u64(sem,argument_const,&count)) goto done;
		if(!count || count>INT32_MAX) goto done;
		pigen_rtl_type_id depth_type=pigen_lower_rtl_type(lowering,
			pigen_data_type_sized_logic(sem,32,PIGEN_SIGN_SIGNED));
		depth=pigen_rtl_expr_add_integer(rtl,depth_type,count,owner_signal->span);
		if (depth.index == PIGEN_INVALID_ID)
			goto done;
		parameter = &depth;
		parameter_count = 1;
	}

#define CONNECT(port, object) \
	connections[connection_count++] = (pigen_rtl_connection){ \
		(port), (object), (pigen_rtl_expr_id){PIGEN_INVALID_ID}, PIGEN_SEMANTIC_INTERNAL}
#define CONNECT_VALUE(port, value) \
	connections[connection_count++] = (pigen_rtl_connection){ \
		(port), (pigen_rtl_object_id){PIGEN_INVALID_ID}, (value), PIGEN_SEMANTIC_INTERNAL}
	if (clock_object.index != PIGEN_INVALID_ID)
		CONNECT("clk", clock_object);
	else
		CONNECT_VALUE("clk", zero);
	if (native_reset.index != PIGEN_INVALID_ID)
		CONNECT_VALUE("reset", native_reset);
	else if (reset_object.index != PIGEN_INVALID_ID)
		CONNECT("reset", reset_object);
	else
		CONNECT_VALUE("reset", zero);
	CONNECT("clear", clear_object);
	CONNECT_VALUE("discard", zero);
	CONNECT("force_valid", force_valid_object);
	CONNECT("force_invalid", force_invalid_object);
	CONNECT("force_after_transfer", force_after_object);
	CONNECT("in_valid", in_valid);
	CONNECT("in_ready", in_ready);
	CONNECT("packet_in", packet_in);
	CONNECT("out_valid", valid_object);
	CONNECT("out_ready", source_ready);
	CONNECT("packet_out", payload);
#undef CONNECT
#undef CONNECT_VALUE

	instance = pigen_rtl_instance_add_with_owner(rtl, rtl_module,
		semantic_module, parameter, parameter_count, NULL, 0,
		owner_signal->span);
	if (instance.index == PIGEN_INVALID_ID)
		goto done;
	connection_range = pigen_rtl_connection_range_add(rtl, connections,
		connection_count);
	if (connection_range.first == (size_t)PIGEN_INVALID_ID)
		goto done;
	if (!pigen_rtl_instance_set_primitive(rtl, instance, primitive,
		data_type, connection_range))
		goto done;
	instance_name = name_join(base, "__pigen_");
	{
		char *full = name_join(instance_name, primitive_stem(primitive));

		free(instance_name);
		instance_name = full;
	}
	if (!instance_name)
		goto done;
	pigen_rtl_set_name(rtl, &rtl->instances[instance.index].name,
		instance_name);
	free(instance_name);

	*out = (pigen_rtl_signal_endpoints){
		payload, valid, ready, packet_in, in_valid, in_ready,
		source_ready, force_valid_object, force_invalid_object,
		clear_object, force_after_object};
	ok = 1;
done:
	free(base);
	return ok;
}

/* Only authored member/index paths into real static record objects may use
 * SV field names. Computed records and buffered channels keep packed lowering. */
static int static_record_path(pigen_rtl_lowering *lowering, pigen_expr_id value)
{
	const pigen_semantic_model *sem = lowering->semantics;
	for (size_t n = 0; n <= sem->expression_count; n++)
	{
		const pigen_semantic_expr *node = pigen_expr_get(sem,value);
		if (!node) return 0;
		switch (node->kind)
		{
		case PIGEN_EXPR_GROUP: value=node->as.group.operand; break;
		case PIGEN_EXPR_PEEK: value=node->as.peek.source; break;
		case PIGEN_EXPR_INDEX: value=node->as.index.base; break;
		case PIGEN_EXPR_SELECT:
			if (!node->as.select.is_member) return 0;
			value=node->as.select.base; break;
		case PIGEN_EXPR_SYMBOL:
		{
			pigen_signal_id signal=pigen_symbol_signal(sem,node->as.symbol);
			if (signal.index >= lowering->lowered_endpoint_count) return 0;
			pigen_rtl_object_id object=lowering->lowered_endpoints[signal.index].payload;
			const pigen_rtl_object *known=pigen_rtl_object_get(lowering->rtl,object);
			return known && known->declaration_type;
		}
		default: return 0;
		}
	}
	return 0;
}

static pigen_rtl_expr_id lower_expr(pigen_rtl_lowering *lowering,
	pigen_expr_id expression);

/* Bind a typed child-port view to the explicitly connected parent triplet. */
static int lower_endpoint_view(pigen_rtl_lowering *lowering, size_t signal_index,
	pigen_rtl_signal_endpoints *out)
{
	pigen_semantic_model *sem = lowering->semantics;
	for (size_t i = 0; i < sem->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &sem->instances[i];
		for (size_t c = 0; c < instance->connection_count; c++)
		{
			const pigen_semantic_instance_connection *connection = &sem->instance_connections[instance->first_connection+c];
			const pigen_semantic_expr *value;
			if (connection->endpoint_view.index != signal_index ||
				sem->signals[signal_index].syntax.index != instance->syntax.index) continue;
			value = pigen_expr_get(sem, connection->value);
			while(value && value->kind==PIGEN_EXPR_CONVERSION) value=pigen_expr_get(sem,value->as.conversion.operand);
			pigen_signal_id payload = {PIGEN_INVALID_ID};
			if (value && value->kind == PIGEN_EXPR_SYMBOL)
				payload = pigen_symbol_signal(sem, value->as.symbol);
			else if (value)
			{
				const pigen_semantic_lvalue *projection = pigen_lvalue_get(sem,
					pigen_lvalue_resolve(sem, (pigen_expr_id){(uint32_t)(value-sem->expressions)}));
				if (projection && projection->kind == PIGEN_LVALUE_PROJECTION)
					payload = projection->as.projection.signal;
			}
			if(payload.index>=lowering->lowered_endpoint_count) return -1;
			*out=lowering->lowered_endpoints[payload.index];
			/* A borrowed object must preserve the endpoint's packed coordinates,
			 * signedness and state domain, not merely its payload width. */
			if (!value || value->kind != PIGEN_EXPR_SYMBOL ||
				value->data_type.index != sem->signals[signal_index].data_type.index)
			{
				const pigen_semantic_signal *view = &sem->signals[signal_index];
				char *name = signal_spelling(lowering, view);
				if (!name) return -1;
				pigen_rtl_type_id type = pigen_lower_rtl_type(lowering, view->data_type);
				pigen_rtl_module_id module = rtl_module_for_semantic(lowering->rtl, sem, view->module);
				pigen_rtl_object_id object = add_named_object(lowering, module, type,
					PIGEN_SEMANTIC_INTERNAL, signal_index, connection->span, name, NULL);
				free(name);
				pigen_expr_id path = (pigen_expr_id){(uint32_t)(value-sem->expressions)};
				pigen_rtl_expr_id expression = lower_expr(lowering,
					connection->endpoint_view_driven ? path : connection->value);
				if (object.index == PIGEN_INVALID_ID || expression.index == PIGEN_INVALID_ID) return -1;
				if (connection->direction == PIGEN_SEMANTIC_INPUT && connection->endpoint_view_driven)
				{
					pigen_rtl_expr_id source = object_expression(lowering, type, object, connection->span);
					if (pigen_rtl_equation_add_projection_with_owner(lowering->rtl, module,
						expression, source, connection->span).index == PIGEN_INVALID_ID) return -1;
				}
				else if (pigen_rtl_equation_add_with_owner(lowering->rtl, module,
					object, expression, connection->span).index == PIGEN_INVALID_ID) return -1;
				out->payload = out->input_payload = object;
			}
			const pigen_symbol *port = pigen_symbol_get(sem,connection->port_symbol);
			size_t base_length=0;
			const char *base=pigen_source_span_text(sem->sources,port->name,&base_length);
			int bound[2] = {0, 0};
			for(size_t d=0;d<instance->connection_count;d++)
			{
				const pigen_semantic_instance_connection *control=&sem->instance_connections[instance->first_connection+d];
				size_t length=0; const char *name=pigen_source_span_text(sem->sources,control->port_name,&length);
				if(!name || length!=base_length+6 || memcmp(name,base,base_length)) continue;
				int ready=!memcmp(name+base_length,"_ready",6);
				if(!ready && memcmp(name+base_length,"_valid",6)) continue;
				bound[ready] = 1;
				const pigen_semantic_expr *expr=pigen_expr_get(sem,control->value);
				while (expr && expr->kind == PIGEN_EXPR_CONVERSION)
					expr = pigen_expr_get(sem, expr->as.conversion.operand);
				if (ready == (connection->direction == PIGEN_SEMANTIC_OUTPUT) &&
					!pigen_lvalue_is_writable(sem, pigen_lvalue_resolve(sem, control->value)))
				{
					/* Read-only controls belong to the explicit handshake owner. */
					pigen_rtl_expr_id observed = lower_expr(lowering, control->value);
					if (observed.index == PIGEN_INVALID_ID) return -1;
					if (ready)
					{
						out->ready = observed;
						out->input_ready = out->source_ready = (pigen_rtl_object_id){PIGEN_INVALID_ID};
					}
					else
					{
						out->valid = observed;
						out->input_valid = (pigen_rtl_object_id){PIGEN_INVALID_ID};
					}
					continue;
				}
				if (!expr || expr->kind != PIGEN_EXPR_SYMBOL)
				{
					/* Preserve selected control bindings through scalar endpoints:
					 * observe child valid, drive child ready into its projection. */
					const pigen_semantic_signal *view = &sem->signals[signal_index];
					char *view_name = signal_spelling(lowering, view);
					if (!view_name) return -1;
					pigen_rtl_type_id type = pigen_lower_rtl_type(lowering,
						pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED));
					pigen_rtl_module_id module = rtl_module_for_semantic(lowering->rtl, sem, view->module);
					pigen_rtl_object_id object = add_named_object(lowering, module,
						type, PIGEN_SEMANTIC_INTERNAL, signal_index, control->span, view_name, ready ? "_ready" : "_valid");
					free(view_name);
					pigen_rtl_expr_id expression = lower_expr(lowering, control->value);
					if (object.index == PIGEN_INVALID_ID || expression.index == PIGEN_INVALID_ID) return -1;
					pigen_rtl_expr_id scalar = object_expression(lowering, type, object, control->span);
					int drive = ready == (connection->direction == PIGEN_SEMANTIC_OUTPUT);
					if (drive)
					{
						if (pigen_rtl_equation_add_projection_with_owner(lowering->rtl, module,
							expression, scalar, control->span).index == PIGEN_INVALID_ID) return -1;
					}
					else
					{
						if (pigen_rtl_equation_add_with_owner(lowering->rtl, module,
							object, expression, control->span).index == PIGEN_INVALID_ID) return -1;
					}
					if (ready) { out->ready = scalar; out->input_ready = object; out->source_ready = object; }
					else { out->valid = scalar; out->input_valid = object; }
					continue;
				}
				pigen_signal_id signal=pigen_symbol_signal(sem,expr->as.symbol);
				if(signal.index>=lowering->lowered_endpoint_count) return -1;
				pigen_rtl_object_id object=lowering->lowered_endpoints[signal.index].payload;
				const pigen_rtl_object *record=pigen_rtl_object_get(lowering->rtl,object);
				if(!record) return -1;
				pigen_rtl_expr_id expression=object_expression(lowering,record->type,object,control->span);
				if(ready) { out->ready=expression; out->input_ready=object; out->source_ready=object; }
				else { out->valid=expression; out->input_valid=object; }
			}
			/* Omitted controls belong to the endpoint view, even when its
			 * payload is explicitly wired to a static parent declaration. */
			for (int ready = 0; ready < 2; ready++)
			{
				if (bound[ready]) continue;
				const pigen_semantic_signal *view = &sem->signals[signal_index];
				char *view_name = signal_spelling(lowering, view);
				if (!view_name) return -1;
				pigen_rtl_type_id type = pigen_lower_rtl_type(lowering,
					pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED));
				pigen_rtl_object_id object = {PIGEN_INVALID_ID};
				char *payload_name = signal_spelling(lowering, &sem->signals[payload.index]);
				char *implicit_name = payload_name ? name_join(payload_name, ready ? "_ready" : "_valid") : NULL;
				free(payload_name);
				for (size_t s = 0; implicit_name && s < sem->signal_count; s++)
				{
					if (sem->signals[s].module.index != view->module.index ||
						s >= lowering->lowered_endpoint_count) continue;
					const pigen_symbol *candidate = pigen_symbol_get(sem, sem->signals[s].symbol);
					size_t length = 0;
					const char *name = pigen_source_span_text(sem->sources, candidate->name, &length);
					if (name && length == strlen(implicit_name) && !memcmp(name, implicit_name, length))
						object = lowering->lowered_endpoints[s].payload;
				}
				free(implicit_name);
				if (object.index == PIGEN_INVALID_ID)
					object = add_named_object(lowering,
						rtl_module_for_semantic(lowering->rtl, sem, view->module), type,
						PIGEN_SEMANTIC_INTERNAL, signal_index, connection->span,
						view_name, ready ? "_ready" : "_valid");
				free(view_name);
				if (object.index == PIGEN_INVALID_ID) return -1;
				pigen_rtl_expr_id expression = object_expression(lowering, type, object, connection->span);
				if (expression.index == PIGEN_INVALID_ID) return -1;
				if (ready) { out->ready = expression; out->input_ready = object; out->source_ready = object; }
				else { out->valid = expression; out->input_valid = object; }
			}
			/* A static projected expression with no automatic writer remains
			 * continuously available unless explicitly qualified by valid. */
			if (connection->direction == PIGEN_SEMANTIC_INPUT && !connection->endpoint_view_driven &&
				value && value->kind != PIGEN_EXPR_SYMBOL && !bound[0])
			{
				const pigen_rtl_object *valid_object = pigen_rtl_object_get(lowering->rtl, out->input_valid);
				if (valid_object && valid_object->semantic_signal.index == signal_index &&
					pigen_rtl_equation_add_with_owner(lowering->rtl,
						rtl_module_for_semantic(lowering->rtl, sem, sem->signals[signal_index].module),
						out->input_valid, control_constant(lowering, 1), connection->span).index == PIGEN_INVALID_ID) return -1;
			}
			return 1;
		}
	}
	return 0;
}

const pigen_rtl_protocol_channel_ports *pigen_lowered_protocol_ports(
	const pigen_rtl_lowering *lowering, pigen_protocol_endpoint_id endpoint,
	pigen_protocol_channel channel)
{
	if (!lowering) return NULL;
	for (size_t i = 0; i < lowering->protocol_port_count; i++)
		if (lowering->protocol_ports[i].endpoint.index == endpoint.index &&
			lowering->protocol_ports[i].channel == channel) return &lowering->protocol_ports[i];
	return NULL;
}

static pigen_data_type_id protocol_field_type(pigen_semantic_model *sem,
	const pigen_semantic_signal *signal, const pigen_protocol_field_layout *field)
{
	pigen_symbol_id alias = pigen_data_type_alias_symbol(sem, signal->data_type);
	for (size_t i = 0; i < sem->struct_member_count; i++)
	{
		const pigen_semantic_struct_member *member = &sem->struct_members[i];
		if (member->owner.index == alias.index && member->offset == field->offset)
			return member->data_type;
	}
	return (pigen_data_type_id){PIGEN_INVALID_ID};
}

/* Resolve standard pins once, above the terminal renderer. Packed channel
 * objects remain internal handshake/storage identities. */
static char *protocol_port_base(const pigen_semantic_model *sem, pigen_protocol_endpoint_id endpoint)
{
	const pigen_semantic_protocol_endpoint *scalar = pigen_protocol_endpoint_get(sem, endpoint);
	size_t ordinal;
	pigen_protocol_array_id owner = pigen_protocol_array_for_endpoint(sem, endpoint, &ordinal);
	const pigen_semantic_protocol_array *array = pigen_protocol_array_get(sem, owner);
	const pigen_symbol *symbol = pigen_symbol_get(sem, array ? array->symbol : scalar->symbol);
	size_t length = 0;
	const char *name = symbol ? pigen_source_span_text(sem->sources, symbol->name, &length) : NULL;
	if (!name) return NULL;
	pigen_string result = {0};
	pigen_append_range(&result, name, length);
	if (array)
	{
		size_t stride = array->element_count;
		for (size_t d = 0; d < array->dimension_count; d++)
		{
			const pigen_protocol_array_dimension *dimension = &sem->protocol_array_dimensions[array->first_dimension+d];
			stride /= dimension->count;
			uint64_t offset = ordinal / stride;
			ordinal %= stride;
			uint64_t coordinate = dimension->left >= dimension->right ?
				(uint64_t)dimension->left - offset : (uint64_t)dimension->left + offset;
			if (coordinate > INT64_MAX)
				pigen_append_format(&result, "_m%llu", (unsigned long long)(UINT64_C(0)-coordinate));
			else pigen_append_format(&result, "_%llu", (unsigned long long)coordinate);
		}
	}
	return result.data;
}

static int lower_protocol_ports(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_rtl_module_id rtl_module)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	for (size_t e = 0; e < sem->protocol_endpoint_count; e++)
	{
		const pigen_semantic_protocol_endpoint *endpoint = &sem->protocol_endpoints[e];
		if (endpoint->module.index != module.index || !endpoint->is_port) continue;
		const pigen_protocol *protocol = pigen_protocol_type_get(sem, endpoint->type);
		char *base = protocol_port_base(sem, (pigen_protocol_endpoint_id){(uint32_t)e});
		if (!protocol || !base) { free(base); return 0; }
		for (pigen_protocol_channel channel = 0; channel < PIGEN_CHANNEL_COUNT; channel++)
		{
			pigen_protocol_channel_layout layout;
			if (!pigen_protocol_layout(protocol, channel, &layout)) continue;
			if (pigen_lowered_protocol_ports(lowering, (pigen_protocol_endpoint_id){(uint32_t)e}, channel)) continue;
			pigen_signal_id id = endpoint->channels[channel];
			const pigen_semantic_signal *signal = pigen_signal_get(sem, id);
			if (!signal || id.index >= lowering->lowered_endpoint_count) { free(base); return 0; }
			pigen_rtl_signal_endpoints endpoints = lowering->lowered_endpoints[id.index];
			const pigen_rtl_expr *valid = pigen_rtl_expr_get(rtl, endpoints.valid);
			if (!valid || valid->kind != PIGEN_RTL_EXPR_OBJECT ||
				endpoints.source_ready.index == PIGEN_INVALID_ID) { free(base); return 0; }
			pigen_rtl_protocol_channel_ports ports = {
				.endpoint = {(uint32_t)e}, .channel = channel,
				.valid = valid->object, .ready = endpoints.source_ready
			};
			pigen_rtl_type_id control_type = valid->type;
			for (size_t f = 0; f < PIGEN_FIELD_COUNT; f++)
				ports.fields[f] = (pigen_rtl_object_id){PIGEN_INVALID_ID};
			const char *prefix = channel == PIGEN_CHANNEL_BEAT ? "t" : pigen_protocol_channel_name(channel);
			pigen_string stem = {0};
			pigen_append(&stem, base); pigen_append(&stem, "_"); pigen_append(&stem, prefix);
			if (signal->direction == PIGEN_SEMANTIC_INPUT)
			{
				ports.valid = add_named_object(lowering, rtl_module, control_type,
					PIGEN_SEMANTIC_INPUT, id.index, endpoint->span, stem.data, "valid");
				ports.ready = add_named_object(lowering, rtl_module, control_type,
					PIGEN_SEMANTIC_OUTPUT, id.index, endpoint->span, stem.data, "ready");
				pigen_rtl_expr_id acceptance;
				if (signal->transfer_type == PIGEN_TRANSFER_TYPE_BUF)
				{
					pigen_data_type_id bit = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
					pigen_unary_resolution inverse;
					if (!pigen_data_type_resolve_unary_operation(sem, PIGEN_UNARY_LOGICAL_NOT, bit, &inverse))
					{ free(stem.data); free(base); return 0; }
					acceptance = pigen_rtl_expr_add_unary(rtl, control_type, &inverse, endpoints.valid, endpoint->span);
				}
				else acceptance = object_expression(lowering, control_type, endpoints.input_ready, endpoint->span);
				pigen_rtl_expr_id offer = object_expression(lowering, control_type, ports.valid, endpoint->span);
				/* A one-slot elastic buffer may replace on a pop. Its AXI
				 * boundary accepts only while empty, cutting that combinational
				 * path while preserving the selected one-slot capacity. */
				if (signal->transfer_type == PIGEN_TRANSFER_TYPE_BUF)
				{
					pigen_data_type_id bit = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
					pigen_binary_resolution conjunction;
					if (!pigen_data_type_resolve_binary_operation(sem, PIGEN_BINARY_LOGICAL_AND,
						bit, bit, &conjunction)) { free(stem.data); free(base); return 0; }
					offer = pigen_rtl_expr_add_binary(rtl, control_type, &conjunction, offer, acceptance, endpoint->span);
				}
				if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, ports.ready, acceptance, endpoint->span).index == PIGEN_INVALID_ID ||
					pigen_rtl_equation_add_with_owner(rtl, rtl_module, endpoints.input_valid, offer, endpoint->span).index == PIGEN_INVALID_ID)
				{ free(stem.data); free(base); return 0; }
			}
			if (!name_object(lowering, ports.valid, name_join(stem.data, "valid")) ||
				!name_object(lowering, ports.ready, name_join(stem.data, "ready")))
			{ free(stem.data); free(base); return 0; }
			rtl->objects[endpoints.payload.index].direction = PIGEN_SEMANTIC_INTERNAL;
			pigen_rtl_type_id payload_type = rtl->objects[endpoints.payload.index].type;
			pigen_rtl_expr_id payload = object_expression(lowering, payload_type, endpoints.payload, endpoint->span);
			pigen_rtl_expr_id pins[PIGEN_FIELD_COUNT];
			pigen_rtl_type_id coordinate_type = pigen_lower_rtl_type(lowering,
				pigen_data_type_sized_logic(sem, 64, PIGEN_SIGN_UNSIGNED));
			for (size_t f = 0; f < layout.field_count; f++)
			{
				const pigen_protocol_field_layout *field = &layout.fields[f];
				pigen_rtl_type_id type = pigen_lower_rtl_type(lowering, protocol_field_type(sem, signal, field));
				pigen_rtl_object_id pin = add_named_object(lowering, rtl_module, type,
					signal->direction, id.index, endpoint->span, stem.data, pigen_protocol_field_name(field->field));
				if (pin.index == PIGEN_INVALID_ID) { free(stem.data); free(base); return 0; }
				ports.fields[field->field] = pin;
				pins[layout.field_count - 1 - f] = object_expression(lowering, type, pin, endpoint->span);
				if (signal->direction == PIGEN_SEMANTIC_OUTPUT)
				{
					pigen_rtl_expr_id high = pigen_rtl_expr_add_integer(rtl, coordinate_type,
						field->offset + field->bits - 1, endpoint->span);
					pigen_rtl_expr_id low = pigen_rtl_expr_add_integer(rtl, coordinate_type, field->offset, endpoint->span);
					pigen_rtl_expr_id value = pigen_rtl_expr_add_select(rtl, type, payload, high, low,
						PIGEN_SEMANTIC_SELECT_RANGE, endpoint->span);
					if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, pin, value, endpoint->span).index == PIGEN_INVALID_ID)
					{ free(stem.data); free(base); return 0; }
				}
			}
			if (signal->direction == PIGEN_SEMANTIC_INPUT)
			{
				pigen_rtl_expr_id value = layout.field_count ? pigen_rtl_expr_add_concatenation(rtl,
					payload_type, pins, layout.field_count, endpoint->span) :
					pigen_rtl_expr_add_integer(rtl, payload_type, 0, endpoint->span);
				if (pigen_rtl_equation_add_with_owner(rtl, rtl_module, endpoints.input_payload, value, endpoint->span).index == PIGEN_INVALID_ID)
				{ free(stem.data); free(base); return 0; }
			}
			free(stem.data);
			if (lowering->protocol_port_count == lowering->protocol_port_capacity)
			{
				lowering->protocol_port_capacity = lowering->protocol_port_capacity ? lowering->protocol_port_capacity * 2 : 8;
				lowering->protocol_ports = pigen_resize(lowering->protocol_ports,
					lowering->protocol_port_capacity * sizeof(*lowering->protocol_ports));
			}
			lowering->protocol_ports[lowering->protocol_port_count++] = ports;
		}
		free(base);
	}
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
	rtl_module = rtl_module_for_semantic(rtl, sem, module);
	if (rtl_module.index == PIGEN_INVALID_ID)
		return -1;

	signal_count = sem->signal_count;
	/* Two passes: static/boundary declarations first so storage primitives can
	 * reference the module clock and reset objects, then storage. */
	for (int pass = 0; pass < 2; pass++)
	for (i = 0; i < signal_count; i++)
	{
		const pigen_semantic_signal *owner_signal = &sem->signals[i];
		const pigen_transfer_type_descriptor *descriptor;
		pigen_transfer_realization realization;
		const struct pigen_realization_endpoint_adapter *adapter;
		pigen_rtl_signal_endpoints endpoints;
		int is_storage;
		int instance_destination;

		if (owner_signal->module.index != module.index)
			continue;
		if (lowering->lowered_endpoint_count > i &&
			lowering->lowered_endpoints[i].payload.index != PIGEN_INVALID_ID)
			continue;
		int view = lower_endpoint_view(lowering, i, &endpoints);
		if (view < 0) return -1;
		if (view)
		{
			if (!endpoint_map_ensure(lowering, i + 1)) return -1;
			lowering->lowered_endpoints[i] = endpoints;
			continue;
		}
		descriptor = pigen_transfer_type_descriptor_get(
			owner_signal->transfer_type);
		if (!descriptor)
			return -1;
		realization = descriptor->realization;
		adapter = &realization_endpoint_adapters[realization];
		if (!adapter->implemented)
			return -1;
		is_storage = realization == PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT ||
			realization == PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER ||
			realization == PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE ||
			realization == PIGEN_TRANSFER_REALIZATION_SKID_QUEUE;
		instance_destination = signal_is_instance_destination(sem,
			owner_signal->symbol);
		const pigen_semantic_protocol_endpoint *protocol_owner = pigen_protocol_endpoint_get(sem,
			pigen_protocol_endpoint_for_signal(sem, (pigen_signal_id){(uint32_t)i}, NULL));
		int native_receiver = protocol_owner && protocol_owner->is_port &&
			owner_signal->direction == PIGEN_SEMANTIC_INPUT;
		/* A concrete buffered input is an upstream-owned boundary view, not
		 * local storage, so it is lowered in the first pass with the
		 * boundary shape. A buffered output driven by a child instance is
		 * likewise the child's boundary view, not a second storage owner. */
		if (is_storage && ((owner_signal->direction == PIGEN_SEMANTIC_INPUT && !native_receiver) ||
			instance_destination || signal_is_child_endpoint(sem, i)))
			is_storage = 0;
		if ((pass == 0) == is_storage)
			continue;

		if (realization == PIGEN_TRANSFER_REALIZATION_BOUNDARY ||
			(is_storage == 0 && realization !=
				PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET &&
				realization != PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE))
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
		else
		{
			pigen_semantic_signal storage_owner = *owner_signal;
			if (native_receiver) storage_owner.direction = PIGEN_SEMANTIC_INTERNAL;
			if (!storage_endpoints(lowering, &storage_owner, i,
				rtl_module, module, realization, &endpoints))
				return -1;
		}

		if (!endpoint_map_ensure(lowering, i + 1) ||
			lowering->lowered_endpoint_count <= i ||
			lowering->lowered_endpoints[i].payload.index != PIGEN_INVALID_ID)
			return -1;
		lowering->lowered_endpoints[i] = endpoints;
	}
 if(!lower_protocol_ports(lowering,module,rtl_module)) return -1;
 for(size_t i=0;i<signal_count;i++)
 {
  const pigen_semantic_signal *signal=&sem->signals[i];
  if(signal->module.index!=module.index || signal->initial_value.index==PIGEN_INVALID_ID) continue;
  pigen_rtl_expr_id value=lower_expr(lowering,signal->initial_value);
  pigen_rtl_object_id object=lowering->lowered_endpoints[i].payload;
  if(value.index==PIGEN_INVALID_ID || object.index==PIGEN_INVALID_ID || rtl->objects[object.index].kind!=PIGEN_RTL_OBJECT_VARIABLE) return -1;
  rtl->objects[object.index].initial_value=value;
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
static pigen_rtl_expr_id fire_and(pigen_rtl_lowering *lowering,
	pigen_rtl_expr_id left, pigen_rtl_expr_id right);

/* Coordinates have a signed concrete domain even when their syntax uses an
 * exact mathematical integer that has no standalone hardware type. */
static pigen_rtl_expr_id lower_coordinate(pigen_rtl_lowering *lowering,
	pigen_expr_id expression)
{
	pigen_semantic_model *sem = lowering->semantics;
	int64_t value;
	if (pigen_const_expr_evaluate_i64(sem, pigen_expr_constant(sem, expression), &value) && value < 0)
	{
		pigen_rtl_type_id type = pigen_lower_rtl_type(lowering,
			pigen_data_type_sized_logic(sem, 64, PIGEN_SIGN_SIGNED));
		pigen_rtl_literal_word word = {(uint64_t)(-(value+1))+1, 0, 0};
		const pigen_semantic_expr *source = pigen_expr_get(sem, expression);
		return pigen_rtl_expr_add_literal(lowering->rtl, PIGEN_RTL_EXPR_INTEGER,
			type, &word, 64, 1, source->span);
	}
	return lower_expr(lowering, expression);
}

static int projection_is_computed(const pigen_semantic_model *model, pigen_expr_id expression)
{
	const pigen_semantic_expr *node=pigen_expr_get(model,expression);
	while (node)
	{
		pigen_expr_id next;
		switch (node->kind)
		{
		case PIGEN_EXPR_SELECT: next=node->as.select.base; break;
		case PIGEN_EXPR_INDEX: next=node->as.index.base; break;
		case PIGEN_EXPR_GROUP: next=node->as.group.operand; break;
		case PIGEN_EXPR_PEEK: next=node->as.peek.source; break;
		case PIGEN_EXPR_EVALUATED_VALUE:
		case PIGEN_EXPR_CONVERSION: case PIGEN_EXPR_ARRAY_PATTERN:
	case PIGEN_EXPR_CONCATENATION:
		case PIGEN_EXPR_BINARY: case PIGEN_EXPR_UNARY: case PIGEN_EXPR_CONDITIONAL: return 1;
		default: return 0;
		}
		node=pigen_expr_get(model,next);
	}
	return 0;
}

static pigen_rtl_module_id projection_module(const pigen_rtl_model *rtl, pigen_rtl_expr_id value)
{
	const pigen_rtl_expr *node = pigen_rtl_expr_get(rtl, value);
	if (!node) return (pigen_rtl_module_id){PIGEN_INVALID_ID};
	if (node->kind == PIGEN_RTL_EXPR_OBJECT)
	{
		const pigen_rtl_object *object = pigen_rtl_object_get(rtl, node->object);
		return object ? object->module : (pigen_rtl_module_id){PIGEN_INVALID_ID};
	}
	size_t count = 0;
	const pigen_rtl_expr_id *children = pigen_rtl_expr_children(rtl, value, &count);
	for (size_t i=0; children && i<count; i++)
	{
		pigen_rtl_module_id module = projection_module(rtl, children[i]);
		if (module.index != PIGEN_INVALID_ID) return module;
	}
	return (pigen_rtl_module_id){PIGEN_INVALID_ID};
}

/* A packed index rooted in an object is a legal primary. Keep procedural
 * coordinate reads in the expression so ordered writes are visible immediately. */
static int projection_is_indexed_primary(const pigen_rtl_model *rtl,pigen_rtl_expr_id value)
{
	for(size_t depth=0;depth<128;depth++)
	{
		const pigen_rtl_expr *node=pigen_rtl_expr_get(rtl,value);
		if(!node) return 0;
		if(node->kind==PIGEN_RTL_EXPR_OBJECT) return 1;
		if(node->kind!=PIGEN_RTL_EXPR_INDEX || node->child_count!=2) return 0;
		value=rtl->expression_children[node->first_child];
	}
	return 0;
}

static int projection_reads_procedural_local(const pigen_semantic_model *model,pigen_expr_id value,size_t depth)
{
	const pigen_semantic_expr *node=pigen_expr_get(model,value);
	if(!node || depth>128) return 0;
	pigen_expr_id children[3];size_t count=0;
	switch(node->kind)
	{
	case PIGEN_EXPR_SYMBOL:
	{
		const pigen_symbol *symbol=pigen_symbol_get(model,node->as.symbol);
		const pigen_scope *scope=symbol ? pigen_scope_get(model,symbol->scope) : NULL;
		return symbol && symbol->kind==PIGEN_SYMBOL_SIGNAL && scope && scope->is_procedural;
	}
	case PIGEN_EXPR_INDEX: children[count++]=node->as.index.base;children[count++]=node->as.index.index;break;
	case PIGEN_EXPR_SELECT: children[count++]=node->as.select.base;children[count++]=node->as.select.left;children[count++]=node->as.select.right;break;
	case PIGEN_EXPR_GROUP: children[count++]=node->as.group.operand;break;
	case PIGEN_EXPR_PEEK: children[count++]=node->as.peek.source;break;
	case PIGEN_EXPR_CONVERSION: children[count++]=node->as.conversion.operand;break;
	case PIGEN_EXPR_UNARY: children[count++]=node->as.unary.operand;break;
	case PIGEN_EXPR_BINARY: children[count++]=node->as.binary.left;children[count++]=node->as.binary.right;break;
	case PIGEN_EXPR_CONDITIONAL: children[count++]=node->as.conditional.condition;children[count++]=node->as.conditional.when_true;children[count++]=node->as.conditional.when_false;break;
	case PIGEN_EXPR_EVALUATED_VALUE: children[count++]=node->as.evaluation.value;children[count++]=node->as.evaluation.evaluated;break;
	case PIGEN_EXPR_ARRAY_PATTERN:
	case PIGEN_EXPR_CONCATENATION:
		for(size_t i=0;i<node->as.sequence.child_count;i++)
			if(projection_reads_procedural_local(model,model->expression_children[node->as.sequence.first_child+i],depth+1)) return 1;
		return 0;
	default: return 0;
	}
	for(size_t i=0;i<count;i++) if(projection_reads_procedural_local(model,children[i],depth+1)) return 1;
	return 0;
}

/* Preserve local coordinates in a typed combinational alias before selecting
 * a native field or an immutable computed record. Mutable ordinary lvalue
 * paths retain their existing lowering. */
static pigen_rtl_expr_id projection_operand(pigen_rtl_lowering *lowering,
	pigen_expr_id semantic_base, pigen_rtl_expr_id value, pigen_source_span span)
{
	const pigen_rtl_expr *rtl_value = pigen_rtl_expr_get(lowering->rtl, value);
	if (!rtl_value || rtl_value->kind == PIGEN_RTL_EXPR_OBJECT) return value;
	if(rtl_value->kind==PIGEN_RTL_EXPR_INDEX && projection_is_indexed_primary(lowering->rtl,value) &&
		projection_reads_procedural_local(lowering->semantics,semantic_base,0)) return value;

	pigen_rtl_type_id type = rtl_value->type;
	const pigen_semantic_expr *base = pigen_expr_get(lowering->semantics, semantic_base);
	int computed = 0;
	while (base && base->kind != PIGEN_EXPR_SYMBOL)
	{
		pigen_expr_id next;
		switch (base->kind)
		{
		case PIGEN_EXPR_SELECT: next = base->as.select.base; break;
		case PIGEN_EXPR_INDEX: next = base->as.index.base; break;
		case PIGEN_EXPR_GROUP: next = base->as.group.operand; break;
		case PIGEN_EXPR_PEEK: next = base->as.peek.source; break;
		case PIGEN_EXPR_CONVERSION: computed=1; next = base->as.conversion.operand; break;
		default: computed=1; next=(pigen_expr_id){PIGEN_INVALID_ID}; break;
		}
		base = pigen_expr_get(lowering->semantics, next);
	}
	pigen_signal_id signal = base ? pigen_symbol_signal(lowering->semantics, base->as.symbol) :
		(pigen_signal_id){PIGEN_INVALID_ID};
	if (!computed && pigen_protocol_endpoint_for_signal(lowering->semantics, signal, NULL).index == PIGEN_INVALID_ID)
	{
		const pigen_semantic_signal *source = pigen_signal_get(lowering->semantics, signal);
		const pigen_transfer_type_descriptor *descriptor = source ? pigen_transfer_type_descriptor_get(source->transfer_type) : NULL;
		if (!descriptor || descriptor->is_static) return value;
	}
	pigen_rtl_module_id module = projection_module(lowering->rtl, value);
	if (module.index == PIGEN_INVALID_ID)
		for (size_t i=0;i<lowering->semantics->module_count;i++)
		{
			const pigen_semantic_module *owner=&lowering->semantics->modules[i];
			if (owner->span.source.index==span.source.index && owner->span.start<=span.start && owner->span.end>=span.end)
			{ module=rtl_module_for_semantic(lowering->rtl,lowering->semantics,(pigen_module_id){(uint32_t)i}); break; }
		}
	if (module.index == PIGEN_INVALID_ID) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	pigen_rtl_object_id object = pigen_rtl_object_add_with_owner(lowering->rtl, module,
		PIGEN_RTL_OBJECT_VARIABLE, type, PIGEN_SEMANTIC_INTERNAL,
		(pigen_signal_id){PIGEN_INVALID_ID}, span);
	if (object.index == PIGEN_INVALID_ID) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	char name[64]; snprintf(name, sizeof name, "__pigen_projection_%u", object.index);
	if (!name_object(lowering, object, pigen_copy_range(name, strlen(name))) ||
		pigen_rtl_equation_add_with_owner(lowering->rtl, module, object, value, span).index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	return object_expression(lowering, type, object, span);
}

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
	{
		pigen_const_expr_id constant = pigen_expr_constant(sem, expression);

		if (constant.index != PIGEN_INVALID_ID &&
			!((owner->kind == PIGEN_EXPR_SELECT && projection_is_computed(sem,owner->as.select.base)) ||
			  (owner->kind == PIGEN_EXPR_INDEX && projection_is_computed(sem,owner->as.index.base))))
		{
			pigen_rtl_expr_id lowered =
				pigen_lower_rtl_expression(lowering, constant);
			uint64_t value;
			pigen_rtl_type_id constant_type;

			if (lowered.index != PIGEN_INVALID_ID)
				return lowered;
			/* Some ordinary-SV literal forms have no constant-record lowering.
			 * Emit their evaluated bit pattern at the expression's type. */
			{
				pigen_const_expr_id evaluated = constant;

				for (int depth = 0; depth < 8; depth++)
				{
					const pigen_const_expr *node =
						pigen_const_expr_get(sem, evaluated);

					if (!node || node->kind !=
						PIGEN_CONST_EXPR_CONVERSION)
						break;
					evaluated = node->as.conversion.operand;
				}
				if (!pigen_const_expr_evaluate_u64(sem, evaluated,
					&value))
					goto ordinary;
			}
			constant_type = pigen_lower_rtl_type(lowering,
				owner->data_type);
			if (constant_type.index == PIGEN_INVALID_ID)
				goto ordinary;
			return pigen_rtl_expr_add_bits(rtl, constant_type, value,
				(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID},
					0, 0});
		}
	}
ordinary:
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
			pigen_parameter_id parameter = pigen_symbol_parameter(sem,
				owner->as.symbol);
			const pigen_semantic_parameter *known =
				pigen_parameter_get(sem, parameter);
			pigen_rtl_type_id type = pigen_lower_rtl_type(lowering,
				symbol->data_type);
			pigen_const_expr_id constant;
			uint64_t value = 0;
			pigen_rtl_literal_word word;

			if (!known || type.index == PIGEN_INVALID_ID)
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			constant = pigen_expr_constant(sem, known->value);
			/* Preserve multiword values and four-state masks through the
			 * ordinary constant adapter before considering a host-word fold. */
			pigen_rtl_expr_id parameter_value = pigen_lower_rtl_expression(lowering, constant);
			if (parameter_value.index != PIGEN_INVALID_ID) return parameter_value;
			if (constant.index == PIGEN_INVALID_ID ||
				!pigen_const_expr_evaluate_u64(sem, constant, &value))
				return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			word = (pigen_rtl_literal_word){value, 0, 0};
			return pigen_rtl_expr_add_literal(lowering->rtl,
				PIGEN_RTL_EXPR_INTEGER, type, &word, 64, 0, owner->span);
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

		{
			pigen_const_expr_id folded = pigen_expr_constant(sem, expression);
			uint64_t value = 0;

			if (folded.index != PIGEN_INVALID_ID &&
				pigen_const_expr_evaluate_u64(sem, folded, &value))
			{
				pigen_rtl_literal_word word = {value, 0, 0};
				return pigen_rtl_expr_add_literal(rtl,
					PIGEN_RTL_EXPR_BITS, result_type, &word, 64, 0,
					origin);
			}
		}
		operand = lower_expr(lowering, owner->as.conversion.operand);
		if (operand.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_rtl_expr_add_conversion(rtl, result_type, conversion,
			operand, origin);
	}
	case PIGEN_EXPR_VALID:
	{
		if (lowering->lowered_endpoint_count <=
			owner->as.control.signal.index)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return lowering->lowered_endpoints[
			owner->as.control.signal.index].valid;
	}
	case PIGEN_EXPR_READY:
	{
		if (lowering->lowered_endpoint_count <=
			owner->as.control.signal.index)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return lowering->lowered_endpoints[
			owner->as.control.signal.index].ready;
	}
	case PIGEN_EXPR_PEEK:
		return lower_expr(lowering, owner->as.peek.source);
	case PIGEN_EXPR_ACCEPTS:
	{
		const pigen_semantic_lvalue *destination =
			pigen_lvalue_get(sem, owner->as.accepts.destination);
		const pigen_semantic_signal *dest_signal;
		pigen_rtl_expr_id valid_term;
		pigen_rtl_expr_id ready_term;

		if (!destination || destination->kind != PIGEN_LVALUE_PROJECTION)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		dest_signal = pigen_signal_get(sem,
			destination->as.projection.signal);
		if (!dest_signal ||
			lowering->lowered_endpoint_count <=
				owner->as.accepts.source_signal.index ||
			lowering->lowered_endpoint_count <=
				destination->as.projection.signal.index)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		valid_term = lowering->lowered_endpoints[
			owner->as.accepts.source_signal.index].valid;
		ready_term = lowering->lowered_endpoints[
			destination->as.projection.signal.index].ready;
		if (valid_term.index == PIGEN_INVALID_ID)
			return ready_term;
		if (ready_term.index == PIGEN_INVALID_ID)
			return valid_term;
		return fire_and(lowering, valid_term, ready_term);
	}
	case PIGEN_EXPR_INDEX:
	{
		pigen_rtl_expr_id base;
		pigen_rtl_expr_id index;

		const pigen_semantic_expr *projection = pigen_expr_get(sem, owner->as.index.base);
		int64_t offset, high, selected;
		if (projection && projection->kind == PIGEN_EXPR_SELECT &&
			!pigen_data_type_dimension_count(sem, projection->data_type) &&
			projection->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE &&
			pigen_const_expr_evaluate_i64(sem, pigen_expr_constant(sem, projection->as.select.left), &high) &&
			pigen_const_expr_evaluate_i64(sem, pigen_expr_constant(sem, projection->as.select.right), &offset) &&
			pigen_const_expr_evaluate_i64(sem, pigen_expr_constant(sem, owner->as.index.index), &selected) &&
			offset >= 0 && high >= offset && selected >= 0 && selected <= high - offset)
		{
			base = lower_expr(lowering, projection->as.select.base);
			base = projection_operand(lowering, projection->as.select.base, base, owner->span);
			pigen_rtl_type_id coordinate = pigen_lower_rtl_type(lowering,
				pigen_data_type_sized_logic(sem, 64, PIGEN_SIGN_UNSIGNED));
			index = pigen_rtl_expr_add_integer(rtl, coordinate, (uint64_t)(offset + selected), origin);
			return pigen_rtl_expr_add_index(rtl, result_type, base, index, origin);
		}

		base = lower_expr(lowering, owner->as.index.base);
		base = projection_operand(lowering, owner->as.index.base, base, owner->span);
		index = lower_coordinate(lowering, owner->as.index.index);
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
		if (owner->as.select.is_member && static_record_path(lowering,owner->as.select.base))
		{
			pigen_semantic_expr member=*owner;
			base=lower_expr(lowering,member.as.select.base);
			left=lower_coordinate(lowering,member.as.select.left);
			right=lower_coordinate(lowering,member.as.select.right);
			size_t length=0;
			const char *source=pigen_source_span_text(sem->sources,member.as.select.member_name,&length);
			if (!source || !length) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			char *name=pigen_copy_range(source,length);
			pigen_rtl_expr_id selected=pigen_rtl_expr_add_member_select(rtl,result_type,base,left,right,name,origin);
			free(name); return selected;
		}
		const pigen_semantic_expr *projection = pigen_expr_get(sem, owner->as.select.base);
		int64_t offset, high, low, projection_high;
		/* Numerical members have local [width-1:0] coordinates. Compose a
		 * constant subrange into their containing record before emitting SV,
		 * whose part-select result cannot itself be part-selected. */
		if (projection && projection->kind == PIGEN_EXPR_SELECT &&
			!pigen_data_type_dimension_count(sem, projection->data_type) &&
			projection->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE &&
			owner->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE &&
			pigen_const_expr_evaluate_i64(sem, pigen_expr_constant(sem, projection->as.select.left), &projection_high) &&
			pigen_const_expr_evaluate_i64(sem, pigen_expr_constant(sem, projection->as.select.right), &offset) &&
			pigen_const_expr_evaluate_i64(sem, pigen_expr_constant(sem, owner->as.select.left), &high) &&
			pigen_const_expr_evaluate_i64(sem, pigen_expr_constant(sem, owner->as.select.right), &low) &&
			offset >= 0 && projection_high >= offset && high >= low && low >= 0 && high <= projection_high - offset)
		{
			base = lower_expr(lowering, projection->as.select.base);
			base = projection_operand(lowering, projection->as.select.base, base, owner->span);
			pigen_rtl_type_id coordinate = pigen_lower_rtl_type(lowering,
				pigen_data_type_sized_logic(sem, 64, PIGEN_SIGN_UNSIGNED));
			left = pigen_rtl_expr_add_integer(rtl, coordinate, (uint64_t)(offset + high), origin);
			right = pigen_rtl_expr_add_integer(rtl, coordinate, (uint64_t)(offset + low), origin);
			return pigen_rtl_expr_add_select(rtl, result_type, base, left, right, PIGEN_SEMANTIC_SELECT_RANGE, origin);
		}

		base = lower_expr(lowering, owner->as.select.base);
		base = projection_operand(lowering, owner->as.select.base, base, owner->span);
		left = lower_coordinate(lowering, owner->as.select.left);
		right = lower_coordinate(lowering, owner->as.select.right);
		if (base.index == PIGEN_INVALID_ID ||
			left.index == PIGEN_INVALID_ID ||
			right.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		return pigen_rtl_expr_add_select(rtl, result_type, base, left,
			right, owner->as.select.kind, origin);
	}
	case PIGEN_EXPR_EVALUATED_VALUE:
	{
		pigen_semantic_expr node=*owner;
		pigen_rtl_expr_id value=lower_expr(lowering,node.as.evaluation.value);
		pigen_rtl_expr_id evaluated=lower_expr(lowering,node.as.evaluation.evaluated);
		return pigen_rtl_expr_add_evaluated_value(rtl,result_type,value,evaluated,node.as.evaluation.before,origin);
	}
	case PIGEN_EXPR_ARRAY_PATTERN:
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
		result = (owner->kind==PIGEN_EXPR_ARRAY_PATTERN ? pigen_rtl_expr_add_array_pattern : pigen_rtl_expr_add_concatenation)(rtl, result_type,
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

static pigen_rtl_expr_id fire_or(pigen_rtl_lowering *lowering,
	pigen_rtl_expr_id left, pigen_rtl_expr_id right)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_data_type_id logic_one = pigen_data_type_sized_logic(sem, 1,
		PIGEN_SIGN_UNSIGNED);
	pigen_rtl_type_id control = pigen_lower_rtl_type(lowering, logic_one);
	pigen_binary_resolution resolution = {
		{PIGEN_CONVERSION_IDENTITY, logic_one, logic_one},
		{PIGEN_CONVERSION_IDENTITY, logic_one, logic_one},
		{PIGEN_BINARY_LOGICAL_OR, logic_one, logic_one, logic_one}};

	if (control.index == PIGEN_INVALID_ID) return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	return pigen_rtl_expr_add_binary(rtl, control, &resolution, left, right,
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
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
	{
		pigen_rtl_expr_id memoized =
			lowering->lowered_expressions[interned.index];
		/* The declaration lowering may already have published the interned
		 * (1, 1-bit-logic) control as an INTEGER constant node. The fire
		 * identity must be a non-constant node, so reuse the memo only when it
		 * is the BITS fire-identity node; otherwise fall through and create it. */
		if (pigen_rtl_expr_get(lowering->rtl, memoized) &&
			pigen_rtl_expr_get(lowering->rtl, memoized)->kind ==
			PIGEN_RTL_EXPR_BITS)
			return memoized;
	}
	/* A 1-bit BITS literal (NOT an integer literal): the fire identity must
	 * be a non-constant node to the shared-node gate. */
	word = (pigen_rtl_literal_word){1, 0, 0};
	lowered = pigen_rtl_expr_add_literal(lowering->rtl, PIGEN_RTL_EXPR_BITS,
		pigen_lower_rtl_type(lowering, logic_one), &word, 1, 0, origin);
	if (lowered.index == PIGEN_INVALID_ID)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	if (!expr_map_ensure(lowering, interned.index + 1) ||
		lowering->lowered_expression_count <= interned.index)
		return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	/* The declaration lowering may have published the interned control as an
	 * INTEGER constant; the fire identity must own the slot, so replace a
	 * non-BITS occupant but never a live fire-identity BITS node. */
	if (lowering->lowered_expressions[interned.index].index !=
		PIGEN_INVALID_ID &&
		pigen_rtl_expr_get(lowering->rtl,
			lowering->lowered_expressions[interned.index]) &&
		pigen_rtl_expr_get(lowering->rtl,
			lowering->lowered_expressions[interned.index])->kind ==
		PIGEN_RTL_EXPR_BITS)
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
	if (!atom->expected)
	{
		pigen_unary_resolution not_resolution = {
			{PIGEN_CONVERSION_IDENTITY, logic_one, logic_one},
			{PIGEN_UNARY_LOGICAL_NOT, logic_one, logic_one}};

		return pigen_rtl_expr_add_unary(rtl, control_type, &not_resolution,
			condition, origin);
	}
	{
		pigen_rtl_literal_word word = {1, 0, 0};

		expected = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_INTEGER,
			control_type, &word, 1, 0, origin);
		if (expected.index == PIGEN_INVALID_ID)
			return (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	}
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

/* The offer identity: the guard atoms conjoined with the distinct consumer
 * valid dependencies. It says the source is offering data; destination
 * readiness is not part of it. */
static pigen_rtl_expr_id offer_identity_for_transfer(
	pigen_rtl_lowering *lowering, const pigen_semantic_transfer *owner)
{
	const pigen_transfer_signal_use *uses;
	const pigen_predicate *guard;
	const pigen_predicate_atom *atoms;
	pigen_rtl_expr_id identity = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	size_t use_count = owner->signal_use_count;
	size_t i;

	uses = use_count ? pigen_transfer_signal_uses(lowering->semantics,
		(pigen_transfer_id){(size_t)(owner - lowering->semantics->transfers)})
		: NULL;
	if (use_count && !uses)
		return identity;
	guard = pigen_predicate_get(lowering->semantics, owner->guard);
	if (!guard)
		return identity;
	atoms = guard->atom_count ?
		pigen_predicate_atoms(lowering->semantics, owner->guard) : NULL;
	for (i = 0; i < guard->atom_count; i++)
	{
		pigen_rtl_expr_id term = fire_atom(lowering, &atoms[i]);

		if (term.index == PIGEN_INVALID_ID)
			return term;
		identity = identity.index == PIGEN_INVALID_ID ? term :
			fire_and(lowering, identity, term);
		if (identity.index == PIGEN_INVALID_ID)
			return identity;
	}
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
			return identity;
	}
	if (identity.index == PIGEN_INVALID_ID)
		identity = fire_bits_one(lowering);
	return identity;
}

static pigen_rtl_expr_id endpoint_offer(pigen_rtl_lowering *lowering,
	const pigen_semantic_transfer *owner, const pigen_semantic_lvalue *destination,
	pigen_rtl_expr_id fire, pigen_rtl_expr_id offer)
{
	if (!destination || destination->kind != PIGEN_LVALUE_PROJECTION ||
		!signal_is_child_endpoint(lowering->semantics, destination->as.projection.signal.index)) return fire;
	const pigen_transfer_signal_use *uses = pigen_transfer_signal_uses(lowering->semantics,
		(pigen_transfer_id){(uint32_t)(owner-lowering->semantics->transfers)});
	for(size_t i=0;i<owner->signal_use_count;i++)
		if((uses[i].roles & PIGEN_TRANSFER_PRODUCER) &&
			uses[i].signal.index!=destination->as.projection.signal.index)
			offer=fire_and(lowering,offer,lowering->lowered_endpoints[uses[i].signal.index].ready);
	return offer;
}

/* Lower validity actions into one equation per controlled storage port, OR-ing
 * every action guard that targets that port. */
static int flush_storage_drives(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_rtl_module_id rtl_module)
{
	pigen_rtl_model *rtl = lowering->rtl;
	size_t i;
	int ok = 1;

	(void)module;
	for (i = 0; i < lowering->storage_drive_count && ok; i++)
	{
		size_t j;
		const pigen_rtl_signal_endpoints *endpoints =
			&lowering->lowered_endpoints[
				lowering->storage_drives[i].signal_index];
		pigen_rtl_expr_id valid = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		pigen_rtl_expr_id payload = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		pigen_rtl_type_id payload_type = endpoints->payload.index ==
			PIGEN_INVALID_ID ? (pigen_rtl_type_id){PIGEN_INVALID_ID} :
			pigen_rtl_object_get(rtl, endpoints->payload)->type;
		pigen_source_span span = lowering->storage_drives[i].span;

		/* Only the first drive for a signal builds the group. */
		for (j = 0; j < i; j++)
			if (lowering->storage_drives[j].signal_index ==
				lowering->storage_drives[i].signal_index)
				break;
		if (j < i) continue;
		for (j = i; j < lowering->storage_drive_count; j++)
		{
			const pigen_rtl_storage_drive *drive =
				&lowering->storage_drives[j];

			if (drive->signal_index !=
				lowering->storage_drives[i].signal_index)
				continue;
			valid = valid.index == PIGEN_INVALID_ID ? drive->fire :
				fire_or(lowering, valid, drive->fire);
			if (valid.index == PIGEN_INVALID_ID) { ok = 0; break; }
			if (payload.index == PIGEN_INVALID_ID)
				payload = drive->value;
			else
			{
				pigen_conditional_resolution resolution = {0};
				pigen_rtl_expr_id zero;

				zero = control_literal(lowering, payload_type, 0, span);
				if (zero.index == PIGEN_INVALID_ID) { ok = 0; break; }
				resolution.operation.result_data_type =
					(pigen_data_type_id){PIGEN_INVALID_ID};
				payload = pigen_rtl_expr_add_conditional(rtl,
					payload_type, &resolution, drive->fire,
					drive->value, payload, span);
				if (payload.index == PIGEN_INVALID_ID) { ok = 0; break; }
				(void)zero;
			}
		}
		if (!ok) break;
		/* Build a nested mux in source order: first drive wins when its
		 * guard fires; otherwise the next. */
		{
			pigen_rtl_expr_id mux = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			pigen_rtl_literal_word word = {0, 0, 0};

			for (j = lowering->storage_drive_count; j-- > i; )
			{
				const pigen_rtl_storage_drive *drive =
					&lowering->storage_drives[j];
				pigen_conditional_resolution resolution = {0};

				if (drive->signal_index !=
					lowering->storage_drives[i].signal_index)
					continue;
				if (mux.index == PIGEN_INVALID_ID)
					mux = pigen_rtl_expr_add_literal(rtl,
						PIGEN_RTL_EXPR_INTEGER, payload_type, &word,
						1, 0, span);
				if (mux.index == PIGEN_INVALID_ID) { ok = 0; break; }
				resolution.operation.result_data_type =
					(pigen_data_type_id){PIGEN_INVALID_ID};
				mux = pigen_rtl_expr_add_conditional(rtl, payload_type,
					&resolution, drive->fire, drive->value, mux, span);
				if (mux.index == PIGEN_INVALID_ID) { ok = 0; break; }
			}
			if (ok)
				ok = pigen_rtl_equation_add_with_owner(rtl, rtl_module,
					endpoints->input_payload, mux, span).index !=
					PIGEN_INVALID_ID &&
					pigen_rtl_equation_add_with_owner(rtl, rtl_module,
						endpoints->input_valid, valid, span).index !=
						PIGEN_INVALID_ID;
		}
	}
	lowering->storage_drive_count = 0;
	return ok ? 0 : -1;
}

static int drive_destination(pigen_rtl_lowering *lowering,
	pigen_rtl_module_id rtl_module, const pigen_semantic_lvalue *lval,
	pigen_rtl_expr_id value, const pigen_semantic_expr *value_expr,
	pigen_rtl_expr_id fire, pigen_source_span span,
	pigen_rtl_update_id *updates, size_t *update_count);

static int drive_concat_rtl_value(pigen_rtl_lowering *lowering,
	pigen_rtl_module_id rtl_module, const pigen_semantic_lvalue *lval,
	pigen_rtl_expr_id value, pigen_data_type_id value_type,
	pigen_rtl_type_id value_rtl_type, pigen_rtl_expr_id fire,
	pigen_source_span span, pigen_rtl_update_id *updates,
	size_t *update_count)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	const pigen_lvalue_id *children;
	pigen_const_expr_id width_expr;
	uint64_t remaining = 0;
	pigen_data_type_id index_data;
	pigen_rtl_type_id index_type;
	size_t i;

	if (lval->kind != PIGEN_LVALUE_CONCATENATION)
		return drive_destination(lowering, rtl_module, lval, value, NULL,
			fire, span, updates, update_count);
	children = pigen_lvalue_children(sem, lval->as.sequence.first_child,
		lval->as.sequence.child_count);
	width_expr = pigen_data_type_packed_width(sem, value_type);
	if (!children || !pigen_const_expr_evaluate_u64(sem, width_expr,
		&remaining) || remaining == 0)
		return 0;
	index_data = pigen_data_type_sized_logic(sem, 32, PIGEN_SIGN_UNSIGNED);
	index_type = pigen_lower_rtl_type(lowering, index_data);
	if (index_type.index == PIGEN_INVALID_ID) return 0;
	for (i = 0; i < lval->as.sequence.child_count; i++)
	{
		const pigen_semantic_lvalue *child = pigen_lvalue_get(sem,
			children[i]);
		pigen_const_expr_id child_width_expr;
		pigen_data_type_id child_data_type;
		uint64_t width = 0;
		pigen_rtl_type_id child_type;
		pigen_rtl_literal_word low_word;
		pigen_rtl_expr_id low, shifted, slice;
		pigen_binary_resolution shift;
		pigen_conversion conversion;

		if (!child) return 0;
		child_data_type = child->data_type;
		child_width_expr = pigen_data_type_packed_width(sem,
			child_data_type);
		if (!pigen_const_expr_evaluate_u64(sem, child_width_expr, &width) ||
			width == 0 || width > remaining)
			return 0;
		if (child->kind == PIGEN_LVALUE_PLACEHOLDER)
		{
			remaining -= width;
			continue;
		}
		child_type = pigen_lower_rtl_type(lowering, child_data_type);
		low_word = (pigen_rtl_literal_word){remaining - width, 0, 0};
		low = pigen_rtl_expr_add_literal(rtl, PIGEN_RTL_EXPR_INTEGER,
			index_type, &low_word, 64, 0, span);
		shift = (pigen_binary_resolution){
			{PIGEN_CONVERSION_IDENTITY, value_type, value_type},
			{PIGEN_CONVERSION_IDENTITY, index_data, index_data},
			{PIGEN_BINARY_SHIFT_RIGHT, value_type, index_data,
				value_type}};
		shifted = pigen_rtl_expr_add_binary(rtl, value_rtl_type, &shift,
			value, low, span);
		if (child_type.index == PIGEN_INVALID_ID ||
			shifted.index == PIGEN_INVALID_ID ||
			!pigen_data_type_resolve_assignment_conversion(sem, value_type,
				child_data_type, &conversion))
			return 0;
		slice = pigen_rtl_expr_add_conversion(rtl, child_type, &conversion,
			shifted, span);
		if (slice.index == PIGEN_INVALID_ID)
			return 0;
		if (child->kind == PIGEN_LVALUE_CONCATENATION)
		{
			if (!drive_concat_rtl_value(lowering, rtl_module, child, slice,
				child_data_type, child_type, fire, span, updates,
				update_count))
				return 0;
		}
		else if (!drive_destination(lowering, rtl_module, child, slice,
			NULL, fire, span, updates, update_count))
			return 0;
		remaining -= width;
	}
	return remaining == 0;
}

static pigen_rtl_expr_id action_conditional(pigen_rtl_lowering *lowering,
	pigen_rtl_type_id type, pigen_rtl_expr_id condition,
	pigen_rtl_expr_id when_true, pigen_rtl_expr_id when_false,
	pigen_source_span span)
{
	pigen_conditional_resolution resolution = {0};

	return pigen_rtl_expr_add_conditional(lowering->rtl, type, &resolution,
		condition, when_true, when_false, span);
}

static int lower_actions(pigen_rtl_lowering *lowering, pigen_module_id module,
	pigen_rtl_module_id rtl_module)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	struct action_state {
		size_t signal;
		pigen_rtl_expr_id valid, invalid, clear;
	};
	struct action_state *states = NULL;
	size_t state_count = 0;
	pigen_data_type_id logic_one = pigen_data_type_sized_logic(sem, 1,
		PIGEN_SIGN_UNSIGNED);
	pigen_rtl_type_id control_type = pigen_lower_rtl_type(lowering, logic_one);
	pigen_source_span no_span = {(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
	size_t i;
	int ok = control_type.index != PIGEN_INVALID_ID;

	if (!ok) return -1;
	for (i = 0; i < sem->action_count && ok; i++)
	{
		const pigen_semantic_action *action = &sem->actions[i];
		const pigen_predicate *predicate;
		const pigen_predicate_atom *atoms;
		struct action_state *state = NULL;
		pigen_rtl_expr_id guard = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		pigen_rtl_expr_id zero = control_literal(lowering, control_type, 0,
			no_span);
		pigen_rtl_expr_id one = control_literal(lowering, control_type, 1,
			no_span);
		size_t j;

		if (action->module.index != module.index) continue;
		if (lowering->lowered_endpoint_count <= action->signal.index) continue;
		predicate = pigen_predicate_get(sem, action->guard);
		if (!predicate || zero.index == PIGEN_INVALID_ID ||
			one.index == PIGEN_INVALID_ID)
		{
			ok = 0;
			break;
		}
		atoms = predicate->atom_count ?
			pigen_predicate_atoms(sem, action->guard) : NULL;
		for (j = 0; j < predicate->atom_count; j++)
		{
			pigen_rtl_expr_id term = fire_atom(lowering, &atoms[j]);

			if (term.index == PIGEN_INVALID_ID) { ok = 0; break; }
			guard = guard.index == PIGEN_INVALID_ID ? term :
				fire_and(lowering, guard, term);
			if (guard.index == PIGEN_INVALID_ID) { ok = 0; break; }
		}
		if (!ok) break;
		if (guard.index == PIGEN_INVALID_ID)
			guard = fire_bits_one(lowering);
		if (guard.index == PIGEN_INVALID_ID) { ok = 0; break; }
		for (j = 0; j < state_count; j++)
			if (states[j].signal == action->signal.index)
			{
				state = &states[j];
				break;
			}
		if (!state)
		{
			states = pigen_resize(states,
				(state_count + 1) * sizeof(*states));
			states[state_count] = (struct action_state){
				action->signal.index, zero, zero, zero};
			state = &states[state_count++];
		}
		if (action->action == PIGEN_ACTION_VALIDATE)
		{
			state->valid = action_conditional(lowering, control_type, guard,
				one, state->valid, no_span);
			state->invalid = action_conditional(lowering, control_type, guard,
				zero, state->invalid, no_span);
		}
		else if (action->action == PIGEN_ACTION_INVALIDATE)
		{
			state->valid = action_conditional(lowering, control_type, guard,
				zero, state->valid, no_span);
			state->invalid = action_conditional(lowering, control_type, guard,
				one, state->invalid, no_span);
		}
		else
			state->clear = action_conditional(lowering, control_type, guard,
				one, state->clear, no_span);
		if (state->valid.index == PIGEN_INVALID_ID ||
			state->invalid.index == PIGEN_INVALID_ID ||
			state->clear.index == PIGEN_INVALID_ID)
			ok = 0;
	}
	for (i = 0; ok && i < state_count; i++)
	{
		const pigen_rtl_signal_endpoints *endpoints =
			&lowering->lowered_endpoints[states[i].signal];
		pigen_rtl_object_id controls[3] = {
			endpoints->force_valid, endpoints->force_invalid,
			endpoints->clear};
		pigen_rtl_expr_id values[3] = {
			states[i].valid, states[i].invalid, states[i].clear};
		size_t c;

		for (c = 0; c < 3; c++)
			if (controls[c].index != PIGEN_INVALID_ID &&
				pigen_rtl_equation_add_with_owner(rtl, rtl_module,
					controls[c], values[c], no_span).index ==
					PIGEN_INVALID_ID)
				ok = 0;
	}
	for (i = 0; ok && i < sem->signal_count; i++)
	{
		const pigen_rtl_signal_endpoints *endpoints;
		pigen_rtl_object_id controls[4];
		size_t c;

		if (sem->signals[i].module.index != module.index ||
			lowering->lowered_endpoint_count <= i) continue;
		endpoints = &lowering->lowered_endpoints[i];
		controls[0] = endpoints->force_valid;
		controls[1] = endpoints->force_invalid;
		controls[2] = endpoints->clear;
		controls[3] = endpoints->force_after_transfer;
		for (c = 0; c < 4 && ok; c++)
		{
			size_t s;
			int driven = controls[c].index == PIGEN_INVALID_ID;

			if (driven) continue;
			if (c < 3)
				for (s = 0; s < state_count; s++)
					if (states[s].signal == i)
					{ driven = 1; break; }
			if (!driven)
			{
				pigen_rtl_expr_id zero = control_literal(lowering,
					control_type, 0, no_span);

				if (zero.index == PIGEN_INVALID_ID ||
					pigen_rtl_equation_add_with_owner(rtl, rtl_module,
						controls[c], zero, no_span).index ==
						PIGEN_INVALID_ID)
					ok = 0;
			}
		}
	}
	free(states);
	return ok ? 0 : -1;
}

static int drive_destination(pigen_rtl_lowering *lowering,
	pigen_rtl_module_id rtl_module, const pigen_semantic_lvalue *lval,
	pigen_rtl_expr_id value, const pigen_semantic_expr *value_expr,
	pigen_rtl_expr_id fire, pigen_source_span span,
	pigen_rtl_update_id *updates, size_t *update_count)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_signal_id dest_signal;
	const pigen_semantic_signal *dest_semantic;
	const pigen_rtl_signal_endpoints *dest_endpoints;
	const pigen_transfer_type_descriptor *dest_descriptor;
	pigen_transfer_realization dest_realization;
	pigen_rtl_type_id dest_type;

	if (!lval || lval->kind != PIGEN_LVALUE_PROJECTION) return 0;
	dest_signal = lval->as.projection.signal;
	if (dest_signal.index == PIGEN_INVALID_ID ||
		lowering->lowered_endpoint_count <= dest_signal.index) return 0;
	dest_semantic = pigen_signal_get(sem, dest_signal);
	dest_endpoints = &lowering->lowered_endpoints[dest_signal.index];
	if (!dest_semantic || dest_endpoints->payload.index == PIGEN_INVALID_ID)
		return 0;
	dest_descriptor = pigen_transfer_type_descriptor_get(
		dest_semantic->transfer_type);
	if (!dest_descriptor) return 0;
	dest_realization = dest_descriptor->realization;
	dest_type = pigen_lower_rtl_type(lowering, lval->data_type);
	if (dest_type.index == PIGEN_INVALID_ID) return 0;
	if (value_expr && value_expr->data_type.index != lval->data_type.index)
	{
		pigen_conversion conversion = {PIGEN_CONVERSION_INTEGER_REINTERPRET,
			value_expr->data_type, lval->data_type};
		value = pigen_rtl_expr_add_conversion(rtl, dest_type, &conversion,
			value, span);
		if (value.index == PIGEN_INVALID_ID) return 0;
	}
	if (dest_realization == PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET)
	{
		pigen_rtl_expr_id zero = control_literal(lowering, dest_type, 0, span);
		pigen_conditional_resolution resolution = {0};
		pigen_rtl_expr_id gated;

		if (zero.index == PIGEN_INVALID_ID) return 0;
		resolution.operation.result_data_type = lval->data_type;
		gated = pigen_rtl_expr_add_conditional(rtl, dest_type, &resolution,
			fire, value, zero, span);
		if (gated.index == PIGEN_INVALID_ID ||
			pigen_rtl_equation_add_with_owner(rtl, rtl_module,
				dest_endpoints->payload, gated, span).index ==
			PIGEN_INVALID_ID) return 0;
	}
	else if (dest_realization == PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE)
	{
		const pigen_semantic_expr *path_shape = lval->expression.index !=
			PIGEN_INVALID_ID ? pigen_expr_get(sem, lval->expression) : NULL;
		pigen_rtl_update_id update_id;

		while (path_shape && path_shape->kind == PIGEN_EXPR_GROUP)
			path_shape = pigen_expr_get(sem,
				path_shape->as.group.operand);
		if (path_shape && (path_shape->kind == PIGEN_EXPR_INDEX ||
			path_shape->kind == PIGEN_EXPR_SELECT))
		{
			pigen_rtl_expr_id path = lower_expr(lowering, lval->expression);

			if (path.index == PIGEN_INVALID_ID) return 0;
			update_id = pigen_rtl_update_add_with_owner(rtl, rtl_module,
				dest_endpoints->payload, value, span);
			if (update_id.index == PIGEN_INVALID_ID ||
				!pigen_rtl_update_set_path(rtl, update_id, path) ||
				!pigen_rtl_update_set_enable(rtl, update_id, fire))
				return 0;
		}
		else
		{
			update_id = pigen_rtl_update_add_with_owner(rtl, rtl_module,
				dest_endpoints->payload, value, span);
			if (update_id.index == PIGEN_INVALID_ID ||
				!pigen_rtl_update_set_enable(rtl, update_id, fire))
				return 0;
		}
		updates[(*update_count)++] = update_id;
	}
	else
	{
		if (dest_endpoints->input_payload.index == PIGEN_INVALID_ID ||
			dest_endpoints->input_valid.index == PIGEN_INVALID_ID)
			return 0;
		if (lowering->storage_drive_count ==
			lowering->storage_drive_capacity)
		{
			lowering->storage_drive_capacity =
				lowering->storage_drive_capacity ?
				lowering->storage_drive_capacity * 2 : 16;
			lowering->storage_drives = pigen_resize(
				lowering->storage_drives,
				lowering->storage_drive_capacity *
					sizeof(*lowering->storage_drives));
		}
		lowering->storage_drives[lowering->storage_drive_count++] =
			(pigen_rtl_storage_drive){dest_signal.index, value, fire, span};
	}
	return 1;
}

/* An explicit child-ready binding is still the readiness of its generated
 * input boundary. Feed it back to structural and parent transfers. */
static int lower_child_input_ready_bindings(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_rtl_module_id rtl_module)
{
	pigen_semantic_model *sem = lowering->semantics;
	for (size_t i = 0; i < sem->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &sem->instances[i];
		if (instance->module.index != module.index) continue;
		for (size_t c = 0; c < instance->connection_count; c++)
		{
			const pigen_semantic_instance_connection *payload =
				&sem->instance_connections[instance->first_connection+c];
			const pigen_symbol *port = pigen_symbol_get(sem, payload->port_symbol);
			size_t n = 0, m = 0;
			const char *name = pigen_source_span_text(sem->sources, payload->port_name, &n);
			const char *base = pigen_source_span_text(sem->sources, port->name, &m);
			if (payload->direction != PIGEN_SEMANTIC_INPUT || !name || !base ||
				n != m || memcmp(name, base, n)) continue;
			const pigen_semantic_expr *value = pigen_expr_get(sem, payload->value);
			while (value && value->kind == PIGEN_EXPR_CONVERSION)
				value = pigen_expr_get(sem, value->as.conversion.operand);
			if (!value || value->kind != PIGEN_EXPR_SYMBOL) continue;
			pigen_signal_id signal = pigen_symbol_signal(sem, value->as.symbol);
			if (signal.index >= lowering->lowered_endpoint_count ||
				!pigen_signal_is_instance_endpoint(sem, signal) ||
				sem->signals[signal.index].direction != PIGEN_SEMANTIC_OUTPUT) continue;
			const pigen_rtl_signal_endpoints *endpoint = &lowering->lowered_endpoints[signal.index];
			const pigen_rtl_expr *ready = pigen_rtl_expr_get(lowering->rtl, endpoint->ready);
			if (!ready || ready->kind != PIGEN_RTL_EXPR_OBJECT) continue;
			pigen_rtl_object_id target = ready->object;
			for (size_t d = 0; d < instance->connection_count; d++)
			{
				const pigen_semantic_instance_connection *control =
					&sem->instance_connections[instance->first_connection+d];
				size_t length = 0;
				const char *control_name = pigen_source_span_text(sem->sources, control->port_name, &length);
				if (!control_name || length != n+6 || memcmp(control_name, name, n) ||
					memcmp(control_name+n, "_ready", 6)) continue;
				pigen_rtl_expr_id expression = lower_expr(lowering, control->value);
				if (expression.index == PIGEN_INVALID_ID ||
					pigen_rtl_equation_add_with_owner(lowering->rtl, rtl_module,
						target, expression, control->span).index == PIGEN_INVALID_ID) return -1;
			}
		}
	}
	return 0;
}

static pigen_rtl_procedural_id lower_combinational_statement(pigen_rtl_lowering *lowering,
	pigen_rtl_module_id module,pigen_procedural_id id,size_t depth)
{
	pigen_semantic_model *sem=lowering->semantics;
	pigen_rtl_procedural_id invalid={PIGEN_INVALID_ID};
	if (id.index>=sem->procedural_count || depth>128) return invalid;
	pigen_semantic_procedural node=sem->procedural[id.index];
	pigen_rtl_expr_id destination={PIGEN_INVALID_ID},value={PIGEN_INVALID_ID},condition={PIGEN_INVALID_ID};
	if (node.kind==PIGEN_PROCEDURAL_ASSIGN)
	{
		const pigen_semantic_transfer *assignment=pigen_transfer_get(sem,node.assignment);
		const pigen_semantic_lvalue *target=assignment ? pigen_lvalue_get(sem,assignment->destination) : NULL;
		if (!target) return invalid;
		destination=lower_expr(lowering,target->expression);value=lower_expr(lowering,assignment->value);
		if (destination.index==PIGEN_INVALID_ID || value.index==PIGEN_INVALID_ID) return invalid;
	}
	else if (node.kind!=PIGEN_PROCEDURAL_BLOCK && node.condition.index!=PIGEN_INVALID_ID)
	{
		condition=lower_expr(lowering,node.condition);
		if (condition.index==PIGEN_INVALID_ID) return invalid;
	}
	if (node.first_child>sem->procedural_child_count || node.child_count>sem->procedural_child_count-node.first_child) return invalid;
	pigen_rtl_procedural_id *children=node.child_count ? pigen_resize(NULL,node.child_count*sizeof(*children)) : NULL;
	for (size_t i=0;i<node.child_count;i++)
	{
		pigen_procedural_id child_id=sem->procedural_children[node.first_child+i];
		if (child_id.index>=sem->procedural_count) {free(children);return invalid;}
		pigen_semantic_procedural child=sem->procedural[child_id.index];
		if (child.case_continuation)
		{
			if (node.kind<PIGEN_PROCEDURAL_CASE || node.kind>PIGEN_PROCEDURAL_CASEX || !i ||
				child.kind!=PIGEN_PROCEDURAL_CASE_ITEM || child.case_continuation!=1 || child.child_count!=1 ||
				child.first_child>=sem->procedural_child_count) {free(children);return invalid;}
			const pigen_semantic_procedural *previous=&sem->procedural[sem->procedural_children[node.first_child+i-1].index];
			if (previous->child_count!=1 || previous->first_child>=sem->procedural_child_count ||
				sem->procedural_children[previous->first_child].index!=sem->procedural_children[child.first_child].index)
				{free(children);return invalid;}
			pigen_rtl_procedural_id body=lowering->rtl->procedural_children[lowering->rtl->procedural[children[i-1].index].first_child];
			pigen_rtl_expr_id label=lower_expr(lowering,child.condition);
			children[i]=pigen_rtl_procedural_add(lowering->rtl,module,PIGEN_PROCEDURAL_CASE_ITEM,
				(pigen_rtl_expr_id){PIGEN_INVALID_ID},(pigen_rtl_expr_id){PIGEN_INVALID_ID},label,&body,1,child.span);
			if (children[i].index!=PIGEN_INVALID_ID) lowering->rtl->procedural[children[i].index].case_continuation=1;
		}
		else children[i]=lower_combinational_statement(lowering,module,child_id,depth+1);
		if (children[i].index==PIGEN_INVALID_ID) { free(children);return invalid; }
	}
	pigen_rtl_procedural_id result=pigen_rtl_procedural_add(lowering->rtl,module,node.kind,
		destination,value,condition,children,node.child_count,node.span);
	if (result.index!=PIGEN_INVALID_ID)
	{
		lowering->rtl->procedural[result.index].case_qualifier=node.case_qualifier;
		lowering->rtl->procedural[result.index].else_if=node.else_if;
	}
	free(children);return result;
}

int pigen_lower_rtl_transfers(pigen_rtl_lowering *lowering,
	pigen_module_id module)
{
	pigen_semantic_model *sem;
	pigen_rtl_model *rtl;
	pigen_rtl_module_id rtl_module;
	pigen_process_id *processes;
	size_t process_count;
	size_t pi;

	if (!lowering || !lowering->semantics || !lowering->rtl)
		return -1;
	sem = lowering->semantics;
	rtl = lowering->rtl;
	if (module.index == PIGEN_INVALID_ID ||
		!pigen_module_get(sem, module))
		return -1;
	if (pigen_lower_rtl_module_declarations(lowering, module))
		return -1;
	fire_memo_reset();
	rtl_module = rtl_module_for_semantic(rtl, sem, module);
	if (rtl_module.index == PIGEN_INVALID_ID)
		return -1;
	if (lower_child_input_ready_bindings(lowering, module, rtl_module)) return -1;
	processes = pigen_module_processes(sem, module, &process_count);
	if (!processes && process_count)
		return -1;
	pigen_rtl_object_id *ready_targets = NULL;
	pigen_rtl_expr_id *ready_terms = NULL;
	size_t ready_group_count = 0;

	for (pi = 0; pi < process_count; pi++)
	{
		const pigen_semantic_process *owner_process;
		const pigen_semantic_clock_domain *owner_domain;
		pigen_rtl_expr_id clock_expr;
		pigen_transfer_id *transfers;
		size_t transfer_count;
		pigen_rtl_update_id *update_ids;
		size_t update_count;
		size_t ti;

		owner_process = pigen_process_get(sem, processes[pi]);
		if (!owner_process)
			return -1;
		if (owner_process->is_combinational)
		{
			pigen_rtl_procedural_id body=lower_combinational_statement(lowering,rtl_module,owner_process->body,0);
			if (body.index==PIGEN_INVALID_ID || pigen_rtl_combinational_process_add(rtl,rtl_module,body,owner_process->span).index==PIGEN_INVALID_ID) return -1;
			continue;
		}
		owner_domain = pigen_clock_domain_get(sem, owner_process->domain);
		clock_expr = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
		if (owner_process->clock.index != PIGEN_INVALID_ID)
		{
			if (!owner_domain) return -1;
			clock_expr = lower_expr(lowering, owner_process->clock);
			if (clock_expr.index == PIGEN_INVALID_ID) return -1;
		}
		transfers = pigen_process_transfers(sem, processes[pi],
			&transfer_count);
		if (!transfers && transfer_count)
			return -1;
		if (transfer_count > SIZE_MAX / sizeof(*update_ids))
			return -1;
		update_ids = pigen_resize(NULL,
			(transfer_count * 8 + 8) * sizeof(*update_ids));
		update_count = 0;
		for (ti = 0; ti < transfer_count; ti++)
		{
			const pigen_semantic_transfer *owner;
			const pigen_semantic_lvalue *lval;
			const pigen_semantic_expr *value_expr;
			pigen_rtl_expr_id fire;
			pigen_rtl_expr_id offer;
			pigen_rtl_expr_id value;

			size_t use_index;

			owner = pigen_transfer_get(sem, transfers[ti]);
			if (!owner)
			{
				free(update_ids);
				return -1;
			}
			if (owner_process->is_continuous)
			{
				const pigen_semantic_lvalue *destination = pigen_lvalue_get(sem, owner->destination);
				pigen_rtl_expr_id path = destination ? lower_expr(lowering, destination->expression) :
					(pigen_rtl_expr_id){PIGEN_INVALID_ID};
				pigen_rtl_expr_id expression = lower_expr(lowering, owner->value);
				if (path.index == PIGEN_INVALID_ID || expression.index == PIGEN_INVALID_ID ||
					pigen_rtl_equation_add_projection_with_owner(rtl, rtl_module,
						path, expression, owner->span).index == PIGEN_INVALID_ID)
				{ free(update_ids); return -1; }
				continue;
			}
			fire = fire_identity_for_transfer(lowering, owner);
			offer = offer_identity_for_transfer(lowering, owner);
			if (fire.index == PIGEN_INVALID_ID ||
				offer.index == PIGEN_INVALID_ID)
			{
				free(update_ids);
				return -1;
			}
			lval = pigen_lvalue_get(sem, owner->destination);
			value_expr = pigen_expr_get(sem, owner->value);
			if (!lval || !value_expr ||
				(value = lower_expr(lowering, owner->value)).index ==
					PIGEN_INVALID_ID)
			{
				free(update_ids);
				return -1;
			}
			if (lval->kind == PIGEN_LVALUE_PROJECTION)
			{
				if (!drive_destination(lowering, rtl_module, lval, value,
					value_expr, endpoint_offer(lowering, owner, lval, fire, offer), owner->span, update_ids, &update_count))
				{
					free(update_ids);
					return -1;
				}
			}
			else if (lval->kind == PIGEN_LVALUE_CONCATENATION)
			{
				const pigen_lvalue_id *dest_children = pigen_lvalue_children(
					sem, lval->as.sequence.first_child,
					lval->as.sequence.child_count);
				const pigen_expr_id *value_children =
					value_expr->kind == PIGEN_EXPR_CONCATENATION ?
					pigen_expr_children(sem,
						value_expr->as.sequence.first_child,
						value_expr->as.sequence.child_count) : NULL;
				size_t c;

				int aligned = dest_children && value_children &&
					lval->as.sequence.child_count ==
						value_expr->as.sequence.child_count;

				if (aligned)
					for (c = 0; c < lval->as.sequence.child_count; c++)
					{
						const pigen_semantic_lvalue *child =
							pigen_lvalue_get(sem, dest_children[c]);
						const pigen_semantic_expr *child_value_expr =
							pigen_expr_get(sem, value_children[c]);
						pigen_const_expr_id dw, vw;
						uint64_t d = 0, v = 0;

						if (child && child->kind ==
							PIGEN_LVALUE_PLACEHOLDER)
							continue;
						if (!child || !child_value_expr)
						{
							aligned = 0;
							break;
						}
						if (lowering->lowered_endpoint_count <=
							child->as.projection.signal.index)
						{
							aligned = 0;
							break;
						}
						dw = pigen_data_type_packed_width(sem,
							child->data_type);
						vw = pigen_data_type_packed_width(sem,
							child_value_expr->data_type);
						if (!pigen_const_expr_evaluate_u64(sem, dw, &d) ||
							!pigen_const_expr_evaluate_u64(sem, vw, &v) ||
							d != v)
						{
							aligned = 0;
							break;
						}
					}
				if (!aligned)
				{
					pigen_const_expr_id total_width =
						pigen_data_type_packed_width(sem,
							value_expr->data_type);
					uint64_t remaining = 0;
					pigen_data_type_id index_data =
						pigen_data_type_sized_logic(sem, 32,
							PIGEN_SIGN_UNSIGNED);
					pigen_rtl_type_id index_type =
						pigen_lower_rtl_type(lowering, index_data);
					pigen_rtl_expr_id whole = lower_expr(lowering,
						owner->value);
					pigen_rtl_type_id whole_type = pigen_lower_rtl_type(
						lowering, value_expr->data_type);

					if (!dest_children ||
						total_width.index == PIGEN_INVALID_ID ||
						!pigen_const_expr_evaluate_u64(sem, total_width,
							&remaining) || remaining == 0 ||
						index_type.index == PIGEN_INVALID_ID ||
						whole.index == PIGEN_INVALID_ID ||
						whole_type.index == PIGEN_INVALID_ID)
					{
						free(update_ids);
						return -1;
					}
					for (c = 0; c < lval->as.sequence.child_count; c++)
					{
						const pigen_semantic_lvalue *child =
							pigen_lvalue_get(sem, dest_children[c]);
						pigen_data_type_id child_data_type;
						pigen_const_expr_id child_width;
						uint64_t width = 0;
						pigen_rtl_type_id child_type;
						pigen_rtl_literal_word low_word;
						pigen_rtl_expr_id low, shifted, slice;
						pigen_binary_resolution shift;
						pigen_conversion conversion;

						if (!child)
						{
							free(update_ids);
							return -1;
						}
						child_data_type = child->data_type;
						child_width = pigen_data_type_packed_width(sem,
							child_data_type);
						if (!pigen_const_expr_evaluate_u64(sem,
							child_width, &width) || width == 0 ||
							width > remaining)
						{
							free(update_ids);
							return -1;
						}
						if (child->kind == PIGEN_LVALUE_PLACEHOLDER)
						{
							remaining -= width;
							continue;
						}
						child_type = pigen_lower_rtl_type(lowering,
							child_data_type);
						low_word = (pigen_rtl_literal_word){
							remaining - width, 0, 0};
						low = pigen_rtl_expr_add_literal(rtl,
							PIGEN_RTL_EXPR_INTEGER, index_type, &low_word,
							64, 0, owner->span);
						shift = (pigen_binary_resolution){
							{PIGEN_CONVERSION_IDENTITY,
								value_expr->data_type,
								value_expr->data_type},
							{PIGEN_CONVERSION_IDENTITY, index_data,
								index_data},
							{PIGEN_BINARY_SHIFT_RIGHT,
								value_expr->data_type, index_data,
								value_expr->data_type}};
						if (child_type.index == PIGEN_INVALID_ID ||
							low.index == PIGEN_INVALID_ID)
						{
							free(update_ids);
							return -1;
						}
						shifted = pigen_rtl_expr_add_binary(rtl, whole_type,
							&shift, whole, low, owner->span);
						if (shifted.index == PIGEN_INVALID_ID ||
							!pigen_data_type_resolve_assignment_conversion(
								sem, value_expr->data_type,
								child_data_type, &conversion))
						{
							free(update_ids);
							return -1;
						}
						slice = pigen_rtl_expr_add_conversion(rtl,
							child_type, &conversion, shifted, owner->span);
						if (slice.index == PIGEN_INVALID_ID ||
							(child->kind == PIGEN_LVALUE_CONCATENATION ?
								!drive_concat_rtl_value(lowering, rtl_module,
									child, slice, child->data_type, child_type,
									fire, owner->span, update_ids,
									&update_count) :
								!drive_destination(lowering, rtl_module, child,
									slice, NULL, endpoint_offer(lowering, owner, child, fire, offer), owner->span, update_ids,
									&update_count)))
						{
							free(update_ids);
							return -1;
						}
						remaining -= width;
					}
				}
				else
				for (c = 0; c < lval->as.sequence.child_count; c++)
				{
					const pigen_semantic_lvalue *child = pigen_lvalue_get(sem,
						dest_children[c]);
					const pigen_semantic_expr *child_value_expr =
						pigen_expr_get(sem, value_children[c]);
					pigen_rtl_expr_id child_value;

					if (child && child->kind ==
						PIGEN_LVALUE_PLACEHOLDER)
						continue;
					child_value = lower_expr(lowering, value_children[c]);
					if (!child || !child_value_expr ||
						child_value.index == PIGEN_INVALID_ID)
					{
						free(update_ids);
						return -1;
					}
					if (child->kind == PIGEN_LVALUE_CONCATENATION)
					{
						pigen_rtl_type_id child_type =
							pigen_lower_rtl_type(lowering,
								child->data_type);

						if (child_type.index == PIGEN_INVALID_ID ||
							!drive_concat_rtl_value(lowering, rtl_module,
								child, child_value, child->data_type,
								child_type, fire, owner->span, update_ids,
								&update_count))
						{
							free(update_ids);
							return -1;
						}
					}
					else if (!drive_destination(lowering, rtl_module, child,
						child_value, child_value_expr, endpoint_offer(lowering, owner, child, fire, offer), owner->span,
						update_ids, &update_count))
					{
						free(update_ids);
						return -1;
					}
				}
			}
			else
			{
				free(update_ids);
				return -1;
			}

			/* Consuming buffered/boundary sources release their token on the
			 * shared fire identity. */
			for (use_index = 0; use_index < owner->signal_use_count;
				use_index++)
			{
				const pigen_transfer_signal_use *use =
					pigen_transfer_signal_uses(sem, transfers[ti]) +
					use_index;
				const pigen_rtl_signal_endpoints *source_endpoints;

				if (!(use->roles & PIGEN_TRANSFER_CONSUMER))
					continue;
				if (lowering->lowered_endpoint_count <=
					use->signal.index)
				{
					free(update_ids);
					return -1;
				}
				source_endpoints =
					&lowering->lowered_endpoints[use->signal.index];
				if (source_endpoints->source_ready.index !=
					PIGEN_INVALID_ID)
				{
					size_t group;

					for (group = 0; group < ready_group_count; group++)
						if (ready_targets[group].index ==
							source_endpoints->source_ready.index)
							break;
					if (group == ready_group_count)
					{
						ready_targets = pigen_resize(ready_targets,
							(ready_group_count + 1) *
								sizeof(*ready_targets));
						ready_terms = pigen_resize(ready_terms,
							(ready_group_count + 1) *
								sizeof(*ready_terms));
						ready_targets[group] =
							source_endpoints->source_ready;
						ready_terms[group] = fire;
						ready_group_count++;
					}
					else
					{
						ready_terms[group] = fire_or(lowering,
							ready_terms[group], fire);
						if (ready_terms[group].index ==
							PIGEN_INVALID_ID)
						{
							free(update_ids);
							return -1;
						}
					}
				}
			}
		}

		if (!owner_domain && update_count)
		{
			free(update_ids);
			return -1;
		}
		if (owner_domain)
		{
			/* Qualified clocked controls keep a check-only ordered tree. The
			 * actual transfers still use their independent fire predicates. */
			pigen_rtl_procedural_id checks={PIGEN_INVALID_ID};
			if (owner_process->body.index!=PIGEN_INVALID_ID)
			{
				checks=lower_combinational_statement(lowering,rtl_module,owner_process->body,0);
				if (checks.index==PIGEN_INVALID_ID) {free(update_ids);return -1;}
			}
			pigen_rtl_process_id proc_id = pigen_rtl_process_add_with_owner(rtl, rtl_module, clock_expr,
				owner_domain->edge, update_ids, update_count,
				owner_process->span);
			if (proc_id.index == PIGEN_INVALID_ID)
			{
				free(update_ids);
				return -1;
			}
			rtl->processes[proc_id.index].body=checks;
		}
		free(update_ids);
	}
	for (pi = 0; pi < ready_group_count; pi++)
		if (pigen_rtl_equation_add_with_owner(rtl, rtl_module,
			ready_targets[pi], ready_terms[pi],
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0})
			.index == PIGEN_INVALID_ID)
		{
			free(ready_targets);
			free(ready_terms);
			return -1;
		}
	{
		pigen_data_type_id logic_one = pigen_data_type_sized_logic(sem, 1,
			PIGEN_SIGN_UNSIGNED);
		pigen_rtl_type_id control_type = pigen_lower_rtl_type(lowering,
			logic_one);
		pigen_rtl_expr_id zero = control_literal(lowering, control_type, 0,
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});

		if (control_type.index == PIGEN_INVALID_ID ||
			zero.index == PIGEN_INVALID_ID)
		{
			free(ready_targets);
			free(ready_terms);
			return -1;
		}
		for (size_t signal_index = 0; signal_index < sem->signal_count;
			signal_index++)
		{
			const pigen_semantic_signal *signal = &sem->signals[signal_index];
			const pigen_rtl_signal_endpoints *endpoints;
			size_t group;
			int driven = 0;

			if (signal->module.index != module.index ||
				signal->direction != PIGEN_SEMANTIC_INPUT ||
				lowering->lowered_endpoint_count <= signal_index)
				continue;
			endpoints = &lowering->lowered_endpoints[signal_index];
			if (endpoints->source_ready.index == PIGEN_INVALID_ID)
				continue;
			for (group = 0; group < ready_group_count; group++)
				if (ready_targets[group].index ==
					endpoints->source_ready.index)
				{ driven = 1; break; }
			for (size_t c = 0; c < sem->instance_connection_count; c++)
				if (sem->instance_connections[c].endpoint_view.index == signal_index || expr_references_symbol(sem,
					sem->instance_connections[c].value, signal->symbol))
				{ driven = 1; break; }
			for (size_t f = 0; f < sem->protocol_fabric_edge_count && !driven; f++)
			{
				const pigen_semantic_protocol_fabric_edge *edge = &sem->protocol_fabric_edges[f];
				const pigen_semantic_protocol_endpoint *source = pigen_protocol_endpoint_get(sem, edge->source);
				const pigen_semantic_protocol_endpoint *destination = pigen_protocol_endpoint_get(sem, edge->destination);
				for (pigen_protocol_channel channel = 0; channel < PIGEN_CHANNEL_COUNT; channel++)
					if (source->channels[channel].index == signal_index || destination->channels[channel].index == signal_index)
					{ driven = 1; break; }
			}
			if (!driven &&
				pigen_rtl_equation_add_with_owner(rtl, rtl_module,
					endpoints->source_ready, zero,
					(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID},
						0, 0}).index == PIGEN_INVALID_ID)
			{
				free(ready_targets);
				free(ready_terms);
				return -1;
			}
		}
	}
	free(ready_targets);
	free(ready_terms);
	if (flush_storage_drives(lowering, module, rtl_module)) return -1;
	if (lower_actions(lowering, module, rtl_module)) return -1;
	return 0;
}

/* Child instances are emitted from semantic connections, so their output
 * drivers are not represented by RTL equations. */
static int child_connection_drives(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_rtl_object_id object)
{
	const pigen_semantic_model *sem = lowering->semantics;
	for (size_t i = 0; i < sem->instance_count; i++)
	{
		const pigen_semantic_instance *instance = &sem->instances[i];
		if (instance->module.index != module.index) continue;
		for (size_t c = 0; c < instance->connection_count; c++)
		{
			const pigen_semantic_instance_connection *connection = &sem->instance_connections[instance->first_connection+c];
			const pigen_semantic_expr *value = pigen_expr_get(sem, connection->value);
			while (value && value->kind == PIGEN_EXPR_CONVERSION)
				value = pigen_expr_get(sem, value->as.conversion.operand);
			if (!value || value->kind != PIGEN_EXPR_SYMBOL) continue;
			pigen_signal_id signal = connection->endpoint_view;
			if (signal.index == PIGEN_INVALID_ID) signal = pigen_symbol_signal(sem, value->as.symbol);
			if (signal.index >= lowering->lowered_endpoint_count) continue;
			const pigen_rtl_signal_endpoints *endpoint = &lowering->lowered_endpoints[signal.index];
			if (connection->direction == PIGEN_SEMANTIC_OUTPUT && endpoint->payload.index == object.index) return 1;
			const pigen_symbol *port = pigen_symbol_get(sem, connection->port_symbol);
			size_t n = 0, m = 0;
			const char *name = pigen_source_span_text(sem->sources, connection->port_name, &n);
			const char *base = pigen_source_span_text(sem->sources, port->name, &m);
			if (!name || !base || n != m || memcmp(name, base, n)) continue;
			int ready = connection->direction == PIGEN_SEMANTIC_INPUT;
			int explicit_control = 0;
			for (size_t d = 0; d < instance->connection_count; d++)
			{
				size_t length = 0;
				const char *control = pigen_source_span_text(sem->sources,
					sem->instance_connections[instance->first_connection+d].port_name, &length);
				if (control && length == n + 6 && !memcmp(control, name, n) &&
					!memcmp(control+n, ready ? "_ready" : "_valid", 6)) explicit_control = 1;
			}
			if (explicit_control) continue;
			const pigen_rtl_expr *control = pigen_rtl_expr_get(lowering->rtl, ready ? endpoint->ready : endpoint->valid);
			if (control && control->kind == PIGEN_RTL_EXPR_OBJECT && control->object.index == object.index) return 1;
		}
	}
	return 0;
}

typedef struct {
	pigen_rtl_connection *items;
	size_t count,capacity;
} generated_connections;

static int generated_connection(pigen_rtl_lowering *lowering,generated_connections *connections,
	const char *port,pigen_rtl_expr_id value,pigen_semantic_direction direction)
{
	const pigen_rtl_expr *expression=pigen_rtl_expr_get(lowering->rtl,value);
	if(!port || !expression) return 0;
	if(connections->count==connections->capacity)
	{
		connections->capacity=connections->capacity ? connections->capacity*2 : 16;
		connections->items=pigen_resize(connections->items,connections->capacity*sizeof(*connections->items));
	}
	connections->items[connections->count++]=(pigen_rtl_connection){
		.port=pigen_copy_range(port,strlen(port)),
		.object=expression->kind==PIGEN_RTL_EXPR_OBJECT ? expression->object : (pigen_rtl_object_id){PIGEN_INVALID_ID},
		.value=value,.direction=direction};
	return 1;
}

static int generated_native_channel(pigen_rtl_lowering *lowering,generated_connections *connections,
	const pigen_semantic_instance_connection *connection,pigen_protocol_endpoint_id endpoint,pigen_protocol_channel channel)
{
	pigen_semantic_model *sem=lowering->semantics;
	const pigen_semantic_protocol_endpoint *port=pigen_protocol_endpoint_get(sem,endpoint);
	const pigen_protocol *protocol=port ? pigen_protocol_type_get(sem,port->type) : NULL;
	pigen_protocol_channel_layout layout;
	const pigen_semantic_expr *value=pigen_expr_get(sem,connection->value);
	while(value && value->kind==PIGEN_EXPR_CONVERSION) value=pigen_expr_get(sem,value->as.conversion.operand);
	pigen_signal_id signal=value && value->kind==PIGEN_EXPR_SYMBOL ? pigen_symbol_signal(sem,value->as.symbol) : (pigen_signal_id){PIGEN_INVALID_ID};
	if(!pigen_protocol_layout(protocol,channel,&layout) || signal.index>=lowering->lowered_endpoint_count) return 0;
	pigen_rtl_signal_endpoints bound=lowering->lowered_endpoints[signal.index];
	const pigen_rtl_object *payload=pigen_rtl_object_get(lowering->rtl,bound.payload);
	if(!payload) return 0;
	pigen_rtl_expr_id packed=object_expression(lowering,payload->type,bound.payload,connection->span);
	pigen_rtl_type_id coordinate=pigen_lower_rtl_type(lowering,pigen_data_type_sized_logic(sem,64,PIGEN_SIGN_UNSIGNED));
	char *base=protocol_port_base(sem,endpoint);
	if(!base) return 0;
	pigen_string stem={0}; pigen_append(&stem,base); free(base); pigen_append(&stem,"_");
	pigen_append(&stem,channel==PIGEN_CHANNEL_BEAT ? "t" : pigen_protocol_channel_name(channel));
	int ok=1;
	for(size_t f=0;f<layout.field_count+2 && ok;f++)
	{
		pigen_rtl_expr_id expression;
		const char *suffix;
		if(f<layout.field_count)
		{
			const pigen_protocol_field_layout *field=&layout.fields[f];
			const pigen_semantic_signal *typed=pigen_signal_get(sem,connection->port_signal);
			pigen_rtl_type_id type=pigen_lower_rtl_type(lowering,protocol_field_type(sem,typed,field));
			pigen_rtl_expr_id high=pigen_rtl_expr_add_integer(lowering->rtl,coordinate,field->offset+field->bits-1,connection->span);
			pigen_rtl_expr_id low=pigen_rtl_expr_add_integer(lowering->rtl,coordinate,field->offset,connection->span);
			expression=pigen_rtl_expr_add_select(lowering->rtl,type,packed,high,low,PIGEN_SEMANTIC_SELECT_RANGE,connection->span);
			suffix=pigen_protocol_field_name(field->field);
		}
		else {expression=f==layout.field_count ? bound.valid : bound.ready; suffix=f==layout.field_count ? "valid" : "ready";}
		char *name=name_join(stem.data,suffix);
		pigen_semantic_direction direction=connection->direction;
		if(f==layout.field_count+1) direction=direction==PIGEN_SEMANTIC_INPUT ? PIGEN_SEMANTIC_OUTPUT : PIGEN_SEMANTIC_INPUT;
		ok=generated_connection(lowering,connections,name,expression,direction); free(name);
	}
	free(stem.data);
	return ok;
}

static int generated_regular_port(pigen_rtl_lowering *lowering,generated_connections *connections,
	const pigen_semantic_instance *instance,const pigen_semantic_instance_connection *connection)
{
	pigen_semantic_model *sem=lowering->semantics;
	const pigen_semantic_signal *port=pigen_signal_get(sem,connection->port_signal);
	const pigen_symbol *symbol=port ? pigen_symbol_get(sem,port->symbol) : NULL;
	size_t length=0,base_length=0;
	const char *name=pigen_source_span_text(sem->sources,connection->port_name,&length);
	const char *base=symbol ? pigen_source_span_text(sem->sources,symbol->name,&base_length) : NULL;
	if(!name || !base) return 0;
	char *spelling=pigen_copy_range(name,length);
	pigen_signal_id view=connection->endpoint_view;
	const pigen_semantic_expr *value=pigen_expr_get(sem,connection->value);
	while(value && value->kind==PIGEN_EXPR_CONVERSION) value=pigen_expr_get(sem,value->as.conversion.operand);
	if(view.index==PIGEN_INVALID_ID && value && value->kind==PIGEN_EXPR_SYMBOL) view=pigen_symbol_signal(sem,value->as.symbol);
	pigen_rtl_signal_endpoints bound={0};
	int have_endpoint=view.index<lowering->lowered_endpoint_count;
	if(have_endpoint) bound=lowering->lowered_endpoints[view.index];
	int payload=length==base_length && !memcmp(name,base,length);
	pigen_rtl_expr_id expression;
	if(connection->endpoint_view.index!=PIGEN_INVALID_ID && payload && have_endpoint)
	{
		const pigen_rtl_object *object=pigen_rtl_object_get(lowering->rtl,bound.payload);
		expression=object ? object_expression(lowering,object->type,bound.payload,connection->span) : (pigen_rtl_expr_id){PIGEN_INVALID_ID};
	}
	else expression=lower_expr(lowering,connection->value);
	int ok=generated_connection(lowering,connections,spelling,expression,connection->direction);
	free(spelling);
	if(!ok || !payload || pigen_transfer_type_descriptor_get(port->transfer_type)->is_static) return ok;
	if(!have_endpoint) return 0;
	for(size_t k=0;k<2;k++)
	{
		const char *suffix=k ? "_ready" : "_valid";
		int explicit_control=0;
		for(size_t c=0;c<instance->connection_count;c++)
		{
			size_t n=0;
			const char *text=pigen_source_span_text(sem->sources,sem->instance_connections[instance->first_connection+c].port_name,&n);
			if(text && n==base_length+6 && !memcmp(text,base,base_length) && !memcmp(text+base_length,suffix,6)) explicit_control=1;
		}
		if(explicit_control) continue;
		char *port_name=pigen_resize(NULL,base_length+7);
		memcpy(port_name,base,base_length);memcpy(port_name+base_length,suffix,7);
		pigen_rtl_expr_id control=k ? bound.ready : bound.valid;
		if(k && connection->direction==PIGEN_SEMANTIC_INPUT && bound.source_ready.index!=PIGEN_INVALID_ID)
		{
			const pigen_rtl_object *ready=pigen_rtl_object_get(lowering->rtl,bound.source_ready);
			if(!ready) {free(port_name);return 0;}
			control=object_expression(lowering,ready->type,bound.source_ready,connection->span);
		}
		ok=generated_connection(lowering,connections,port_name,control,k ?
			(connection->direction==PIGEN_SEMANTIC_INPUT ? PIGEN_SEMANTIC_OUTPUT : PIGEN_SEMANTIC_INPUT) : connection->direction);
		free(port_name); if(!ok) return 0;
	}
	return 1;
}

int pigen_lower_rtl_generated_instances(pigen_rtl_lowering *lowering,pigen_module_id module)
{
	if(!lowering || !lowering->semantics || !lowering->rtl) return -1;
	pigen_semantic_model *sem=lowering->semantics;
	const pigen_semantic_module *owner=pigen_module_get(sem,module);
	if(!owner) return -1;
	pigen_rtl_module_id rtl_module=rtl_module_for_semantic(lowering->rtl,sem,module);
	for(size_t i=0;i<sem->instance_count;i++)
	{
		const pigen_semantic_instance *instance=&sem->instances[i];
		const pigen_symbol *symbol=pigen_symbol_get(sem,instance->symbol);
		if(instance->module.index!=module.index || (symbol->scope.index==owner->scope.index && owner->specialization_of.index==PIGEN_INVALID_ID &&
			sem->modules[instance->child_module.index].specialization_of.index==PIGEN_INVALID_ID &&
			!sem->modules[instance->child_module.index].is_external)) continue;
		int exists=0;
		for(size_t p=0;p<lowering->rtl->instance_count;p++)
			if(lowering->rtl->instances[p].definition && lowering->rtl->instances[p].semantic_instance.index==i) exists=1;
		if(exists) continue;
		generated_connections connections={0}; int ok=1;
		for(size_t c=0;c<instance->connection_count && ok;c++)
		{
			const pigen_semantic_instance_connection *connection=&sem->instance_connections[instance->first_connection+c];
			pigen_protocol_channel channel;
			pigen_protocol_endpoint_id endpoint=pigen_protocol_endpoint_for_signal(sem,connection->port_signal,&channel);
			ok=endpoint.index!=PIGEN_INVALID_ID ? generated_native_channel(lowering,&connections,connection,endpoint,channel) :
				generated_regular_port(lowering,&connections,instance,connection);
		}
		pigen_rtl_record_range range={0};
		if(ok) {range=pigen_rtl_connection_range_add(lowering->rtl,connections.items,connections.count);ok=range.first!=PIGEN_INVALID_ID;}
		for(size_t c=0;c<connections.count;c++) free((void*)connections.items[c].port);
		free(connections.items);
		if(!ok) return -1;
		const pigen_semantic_module *child = pigen_module_get(sem, instance->child_module);
		const pigen_semantic_module *definition_owner = child;
		while (definition_owner->is_external && definition_owner->specialization_of.index != PIGEN_INVALID_ID)
			definition_owner = pigen_module_get(sem, definition_owner->specialization_of);
		const pigen_symbol *target=pigen_symbol_get(sem,definition_owner->symbol);
		size_t length=0; const char *definition=pigen_source_span_text(sem->sources,target->name,&length);
		char *name=scoped_symbol_spelling(sem,symbol);
		if(!definition || !name) {free(name);return -1;}
		pigen_rtl_expr_id *parameters = NULL;
		size_t parameter_count = 0;
		if (child->is_external)
			for (size_t p = 0; p < sem->parameter_count; p++) {
				const pigen_semantic_parameter *parameter = &sem->parameters[p];
				const pigen_symbol *parameter_symbol = pigen_symbol_get(sem, parameter->symbol);
				if (parameter->module.index != instance->child_module.index || parameter->is_local ||
					parameter_symbol->scope.index != child->scope.index) continue;
				pigen_rtl_expr_id value = pigen_lower_rtl_expression(lowering, pigen_expr_constant(sem, parameter->value));
				if (value.index == PIGEN_INVALID_ID) { free(parameters); free(name); return -1; }
				parameters = pigen_resize(parameters, (parameter_count + 1) * sizeof(*parameters));
				parameters[parameter_count++] = value;
			}
		pigen_rtl_instance_id id=pigen_rtl_instance_add_with_owner(lowering->rtl,rtl_module,module,parameters,parameter_count,NULL,0,instance->span);
		free(parameters);
		if(id.index==PIGEN_INVALID_ID) {free(name);return -1;}
		pigen_rtl_instance *record=&lowering->rtl->instances[id.index];
		record->primitive=PIGEN_RTL_PRIMITIVE_INVALID;
		record->payload_type=(pigen_rtl_type_id){PIGEN_INVALID_ID};
		record->semantic_instance=(pigen_instance_id){(uint32_t)i};
		record->connection_records=range;
		pigen_rtl_set_name(lowering->rtl,&record->name,name); free(name);
		record->definition=pigen_copy_range(definition,length);
	}
	return 0;
}

int pigen_lower_rtl_endpoint_defaults(pigen_rtl_lowering *lowering,
	pigen_module_id module)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_rtl_module_id owner = rtl_module_for_semantic(rtl, sem, module);
	for (size_t s = 0; s < sem->signal_count; s++)
	{
		const pigen_semantic_signal *signal = &sem->signals[s];
		if (signal->module.index != module.index || !signal_is_child_endpoint(sem, s) ||
			s >= lowering->lowered_endpoint_count) continue;
		const pigen_rtl_signal_endpoints *endpoint = &lowering->lowered_endpoints[s];
		pigen_rtl_object_id targets[2] = {endpoint->source_ready, {PIGEN_INVALID_ID}};
		if (signal->direction == PIGEN_SEMANTIC_OUTPUT)
		{
			targets[0] = endpoint->input_payload;
			targets[1] = endpoint->input_valid;
		}
		for (size_t t = 0; t < 2; t++)
		{
			const pigen_rtl_object *object = pigen_rtl_object_get(rtl, targets[t]);
			/* Borrowed explicit parent controls retain their existing owner. */
			if (!object || object->semantic_signal.index != s) continue;
			int driven = child_connection_drives(lowering, module, targets[t]);
			for (size_t i = 0; !driven && i < rtl->instance_count; i++)
			{
				const pigen_rtl_instance *instance = &rtl->instances[i];
				if (instance->module.index != owner.index || instance->primitive == PIGEN_RTL_PRIMITIVE_INVALID) continue;
				for (size_t c = 0; c < instance->connection_records.count; c++)
				{
					const pigen_rtl_connection *connection = &rtl->connections[instance->connection_records.first+c];
					if (connection->object.index != targets[t].index || !connection->port) continue;
					if (pigen_rtl_primitive_port_is_output(instance->primitive, connection->port)) driven = 1;
				}
			}
			for (size_t e = 0; !driven && e < rtl->equation_count; e++)
				if (pigen_rtl_equation_drives_object(rtl, (pigen_rtl_equation_id){(uint32_t)e}, targets[t])) driven = 1;
			for (size_t p = 0; !driven && p < rtl->process_count; p++)
				if (rtl->processes[p].module.index==owner.index &&
					pigen_rtl_process_drives_object(rtl,(pigen_rtl_process_id){(uint32_t)p},targets[t])) driven=1;
			for (size_t u = 0; !driven && u < rtl->update_count; u++)
				if (rtl->updates[u].destination.index == targets[t].index) driven = 1;
			if (driven) continue;
			pigen_rtl_expr_id zero = pigen_rtl_expr_add_bits(rtl, object->type, 0, signal->span);
			if (zero.index == PIGEN_INVALID_ID || pigen_rtl_equation_add_with_owner(rtl,
				owner, targets[t], zero, signal->span).index == PIGEN_INVALID_ID) return -1;
		}
	}
	return 0;
}

/* Task 8 (6) module composition: returns the published module identity whose
 * four record ranges - objects, instances, equations and processes - cover
 * exactly the witness's records in arena order. The transfer lowering
 * (pigen_lower_rtl_transfers) already published those records through the
 * owner constructors, and each constructor extends the module's matching owner
 * range as it appends; a module's ranges therefore sit at the tail of each
 * arena and grow only with the records it owns. The composition reuses that
 * publication state and does not re-derive it (it does NOT re-run the transfer
 * lowering, which is non-idempotent: a repeat run would reset the module memo,
 * recreate the module and re-append records). It resolves the module through
 * the memo, checks each of the four ranges is exactly the arena tail, and that
 * the processes and per-process updates match the semantic owner (one RTL
 * process per semantic process, in arena order, each owning exactly its own
 * updates, each update's destination the destination signal's payload object
 * and its value the transfer's shared fire identity, equal to the source-ready
 * equation's value). A merged, missing or reordered process, or a range that
 * admits a foreign record or drops the witness's own, fails the gate. The
 * invalid return is the owner's rejection path only: a failure may leave
 * partial state behind and is not a rollback contract
 * (how/should/compiler/builders/fail.md). */
static int lower_specialization_parameters(pigen_rtl_lowering *lowering,pigen_module_id module)
{
	pigen_semantic_model *sem=lowering->semantics;pigen_rtl_model *rtl=lowering->rtl;
	const pigen_semantic_module *owner=pigen_module_get(sem,module);
	if(owner->specialization_of.index==PIGEN_INVALID_ID) return 0;
	pigen_rtl_module_id target=rtl_module_for_semantic(rtl,sem,module);
	for(size_t i=0;i<sem->parameter_count;i++)
	{
		const pigen_semantic_parameter *parameter=&sem->parameters[i];
		if(parameter->module.index!=module.index) continue;
		const pigen_symbol *symbol=pigen_symbol_get(sem,parameter->symbol);
		if(symbol->scope.index!=owner->scope.index) continue;
		int exists=0;
		for(size_t j=0;j<rtl->object_count;j++)
			if(rtl->objects[j].module.index==target.index && rtl->objects[j].kind==PIGEN_RTL_OBJECT_PARAMETER &&
				rtl->objects[j].origin.source.index==symbol->name.source.index &&
				rtl->objects[j].origin.start==symbol->name.start && rtl->objects[j].origin.end==symbol->name.end) exists=1;
		if(exists) continue;
		pigen_rtl_type_id type=pigen_lower_rtl_type(lowering,symbol->data_type);
		pigen_rtl_expr_id value=pigen_lower_rtl_expression(lowering,pigen_expr_constant(sem,parameter->value));
		if(type.index==PIGEN_INVALID_ID || value.index==PIGEN_INVALID_ID) return -1;
		pigen_rtl_object_id object=pigen_rtl_object_add_with_owner(rtl,target,PIGEN_RTL_OBJECT_PARAMETER,type,
			PIGEN_SEMANTIC_INTERNAL,(pigen_signal_id){PIGEN_INVALID_ID},symbol->name);
		if(object.index==PIGEN_INVALID_ID) return -1;
		rtl->objects[object.index].initial_value=value;
		size_t length=0;const char *name=pigen_source_span_text(sem->sources,symbol->name,&length);
		if(!name) return -1;
		char *copy=pigen_copy_range(name,length);pigen_rtl_set_name(rtl,&rtl->objects[object.index].name,copy);free(copy);
	}
	return 0;
}

/* External source paths use selected scope metadata; emitted spellings remain
 * the resolved RTL names. Keep the relation explicit instead of parsing names. */
static char *source_scope_path(const pigen_semantic_model *sem,pigen_module_id module,pigen_scope_id scope)
{
	const pigen_semantic_module *owner=pigen_module_get(sem,module);
	if(!owner) return NULL;
	const pigen_semantic_generate_scope **parents=pigen_resize(NULL,(sem->generate_scope_count+1)*sizeof(*parents));
	size_t count=0,depth=0;
	while(scope.index!=owner->scope.index)
	{
		const pigen_scope *current=pigen_scope_get(sem,scope);
		if(!current || depth++>=sem->scope_count) {free(parents);return NULL;}
		for(size_t i=0;i<sem->generate_scope_count;i++)
			if(sem->generate_scopes[i].scope.index==scope.index) parents[count++]=&sem->generate_scopes[i];
		scope=current->parent;
	}
	pigen_string path={0};
	for(size_t i=count;i>0;i--)
	{
		const pigen_semantic_generate_scope *part=parents[i-1];
		if(!part->external_name) {free(parents);free(path.data);return NULL;}
		if(path.length) pigen_append(&path,".");
		pigen_append(&path,part->external_name);
		if(part->is_iteration) pigen_append_format(&path,"[%lld]",(long long)part->index);
	}
	free(parents);return path.data ? path.data : pigen_copy_range("",0);
}

static char *source_symbol_path(const pigen_semantic_model *sem,pigen_module_id module,pigen_symbol_id id)
{
	const pigen_symbol *symbol=pigen_symbol_get(sem,id);
	size_t length=0;const char *name=symbol ? pigen_source_span_text(sem->sources,symbol->name,&length) : NULL;
	char *scope=symbol ? source_scope_path(sem,module,symbol->scope) : NULL;
	if(!name || !scope) {free(scope);return NULL;}
	pigen_string path={0};pigen_append(&path,scope);free(scope);
	if(path.length) pigen_append(&path,".");
	pigen_append_range(&path,name,length);return path.data;
}

static char *source_endpoint_path(const pigen_semantic_model *sem,pigen_protocol_endpoint_id id,size_t depth)
{
	const pigen_semantic_protocol_endpoint *endpoint=pigen_protocol_endpoint_get(sem,id);
	if(!endpoint || depth>sem->instance_count+1) return NULL;
	if(endpoint->is_instance_view)
	{
		const pigen_semantic_instance *instance=pigen_instance_get(sem,endpoint->instance);
		char *parent=instance ? source_symbol_path(sem,endpoint->module,instance->symbol) : NULL;
		char *child=source_endpoint_path(sem,endpoint->child_endpoint,depth+1);
		if(!parent || !child) {free(parent);free(child);return NULL;}
		pigen_string path={0};pigen_append(&path,parent);pigen_append(&path,".");pigen_append(&path,child);
		free(parent);free(child);return path.data;
	}
	size_t ordinal=0;
	const pigen_semantic_protocol_array *array=pigen_protocol_array_get(sem,pigen_protocol_array_for_endpoint(sem,id,&ordinal));
	char *base=source_symbol_path(sem,endpoint->module,array ? array->symbol : endpoint->symbol);
	if(!base) return NULL;
	pigen_string path={0};pigen_append(&path,base);free(base);
	if(array)
	{
		size_t stride=array->element_count;
		for(size_t d=0;d<array->dimension_count;d++)
		{
			const pigen_protocol_array_dimension *dimension=&sem->protocol_array_dimensions[array->first_dimension+d];
			stride/=dimension->count;
			uint64_t offset=ordinal/stride;ordinal%=stride;
			uint64_t coordinate=dimension->left>=dimension->right ? (uint64_t)dimension->left-offset : (uint64_t)dimension->left+offset;
			if(coordinate>INT64_MAX) pigen_append_format(&path,"[-%llu]",(unsigned long long)(UINT64_C(0)-coordinate));
			else pigen_append_format(&path,"[%llu]",(unsigned long long)coordinate);
		}
	}
	return path.data;
}

static char *source_signal_path(const pigen_semantic_model *sem,pigen_signal_id id)
{
	const pigen_semantic_signal *signal=pigen_signal_get(sem,id);
	if(!signal) return NULL;
	pigen_protocol_channel channel;
	pigen_protocol_endpoint_id endpoint=pigen_protocol_endpoint_for_signal(sem,id,&channel);
	if(endpoint.index==PIGEN_INVALID_ID)
	{
		for(size_t i=0;i<sem->instance_count;i++)
			for(size_t c=0;c<sem->instances[i].connection_count;c++)
			{
				const pigen_semantic_instance_connection *connection=&sem->instance_connections[sem->instances[i].first_connection+c];
				if(connection->endpoint_view.index!=id.index) continue;
				const pigen_symbol *port=pigen_symbol_get(sem,connection->port_symbol);
				char *parent=source_symbol_path(sem,signal->module,sem->instances[i].symbol);
				size_t length=0;const char *name=port ? pigen_source_span_text(sem->sources,port->name,&length) : NULL;
				if(!parent || !name) {free(parent);return NULL;}
				pigen_string path={0};pigen_append(&path,parent);pigen_append(&path,".");pigen_append_range(&path,name,length);
				free(parent);return path.data;
			}
		return source_symbol_path(sem,signal->module,signal->symbol);
	}
	char *base=source_endpoint_path(sem,endpoint,0);
	if(!base) return NULL;
	pigen_string path={0};pigen_append(&path,base);free(base);
	pigen_append(&path,".");pigen_append(&path,pigen_protocol_channel_name(channel));return path.data;
}

static int source_object_reference(pigen_rtl_lowering *lowering,pigen_rtl_module_id module,
	pigen_signal_id signal,pigen_rtl_source_role role,pigen_rtl_object_id object,const char *path)
{
	if(object.index==PIGEN_INVALID_ID) return 1;
	const pigen_rtl_object *target=pigen_rtl_object_get(lowering->rtl,object);
	const pigen_semantic_signal *source=pigen_signal_get(lowering->semantics,signal);
	const pigen_symbol *symbol=source ? pigen_symbol_get(lowering->semantics,source->symbol) : NULL;
	if(!target || !target->name || !symbol) return 0;
	pigen_rtl_source_reference reference={.role=role,.module=module,.scope=symbol->scope,
		.signal=signal,.object=object,.instance={PIGEN_INVALID_ID},.source_path=(char*)path,.emitted_name=target->name};
	return pigen_rtl_source_reference_add(lowering->rtl,&reference)!=SIZE_MAX;
}

static pigen_rtl_object_id source_control_object(const pigen_rtl_model *rtl,pigen_rtl_expr_id id)
{
	const pigen_rtl_expr *expression=pigen_rtl_expr_get(rtl,id);
	return expression && expression->kind==PIGEN_RTL_EXPR_OBJECT ? expression->object : (pigen_rtl_object_id){PIGEN_INVALID_ID};
}

static char *source_instance_port_path(pigen_rtl_lowering *lowering,
	const pigen_semantic_instance *instance,const char *port)
{
	pigen_semantic_model *sem=lowering->semantics;
	char *base=source_symbol_path(sem,instance->module,instance->symbol);
	if(!base) return NULL;
	char *suffix=NULL;
	for(size_t e=0;e<sem->protocol_endpoint_count && !suffix;e++)
	{
		const pigen_semantic_protocol_endpoint *endpoint=&sem->protocol_endpoints[e];
		if(endpoint->module.index!=instance->child_module.index || !endpoint->is_port) continue;
		const pigen_protocol *protocol=pigen_protocol_type_get(sem,endpoint->type);
		char *pin_base=protocol_port_base(sem,(pigen_protocol_endpoint_id){(uint32_t)e});
		if(!pin_base) {free(base);return NULL;}
		for(pigen_protocol_channel channel=0;channel<PIGEN_CHANNEL_COUNT && !suffix;channel++)
		{
			pigen_protocol_channel_layout layout;
			if(!pigen_protocol_layout(protocol,channel,&layout)) continue;
			for(size_t f=0;f<layout.field_count+2 && !suffix;f++)
			{
				const char *field=f<layout.field_count ? pigen_protocol_field_name(layout.fields[f].field) : f==layout.field_count ? "valid" : "ready";
				pigen_string pin={0};pigen_append(&pin,pin_base);pigen_append(&pin,"_");
				pigen_append(&pin,channel==PIGEN_CHANNEL_BEAT ? "t" : pigen_protocol_channel_name(channel));pigen_append(&pin,field);
				int match=!strcmp(pin.data,port);free(pin.data);
				if(match)
				{
					char *endpoint_path=source_endpoint_path(sem,(pigen_protocol_endpoint_id){(uint32_t)e},0);
					if(!endpoint_path) {free(pin_base);free(base);return NULL;}
					pigen_string path={0};pigen_append(&path,endpoint_path);free(endpoint_path);
					pigen_append(&path,".");pigen_append(&path,pigen_protocol_channel_name(channel));pigen_append(&path,".");pigen_append(&path,field);
					suffix=path.data;
				}
			}
		}
		free(pin_base);
	}
	pigen_string path={0};pigen_append(&path,base);pigen_append(&path,".");pigen_append(&path,suffix ? suffix : port);
	free(base);free(suffix);return path.data;
}

static int lower_retained_observations(pigen_rtl_lowering *lowering,pigen_module_id module,pigen_rtl_module_id rtl_module)
{
 pigen_semantic_model *sem=lowering->semantics;
 if(!lowering->retained_observation_objects && sem->retained_observation_count)
 {
  lowering->retained_observation_count=sem->retained_observation_count;
  lowering->retained_observation_objects=pigen_resize(NULL,sem->retained_observation_count*sizeof(pigen_rtl_object_id));
  for(size_t i=0;i<sem->retained_observation_count;i++) lowering->retained_observation_objects[i]=(pigen_rtl_object_id){PIGEN_INVALID_ID};
 }
 for(size_t i=0;i<sem->retained_observation_count;i++)
 {
  const pigen_semantic_retained_observation *use=&sem->retained_observations[i];
  if(use->module.index!=module.index || lowering->retained_observation_objects[i].index!=PIGEN_INVALID_ID) continue;
  const pigen_semantic_expr *value=pigen_expr_get(sem,use->value);
  if(!value) return 0;
  pigen_source_span span=value->span;
  pigen_data_type_id data_type=value->data_type;
  pigen_rtl_expr_id expression=lower_expr(lowering,use->value);
  pigen_rtl_type_id type=pigen_lower_rtl_type(lowering,data_type);
  if(expression.index==PIGEN_INVALID_ID || type.index==PIGEN_INVALID_ID) return 0;
  pigen_rtl_object_id object=pigen_rtl_object_add_with_owner(lowering->rtl,rtl_module,
   PIGEN_RTL_OBJECT_VARIABLE,type,PIGEN_SEMANTIC_INTERNAL,(pigen_signal_id){PIGEN_INVALID_ID},span);
  char name[80];snprintf(name,sizeof name,"__pigen_retained_observation_%u",object.index);
  if(object.index==PIGEN_INVALID_ID || !name_object(lowering,object,pigen_copy_range(name,strlen(name))) ||
   pigen_rtl_equation_add_with_owner(lowering->rtl,rtl_module,object,expression,span).index==PIGEN_INVALID_ID) return 0;
  lowering->retained_observation_objects[i]=object;
 }
 return 1;
}

static int lower_source_references(pigen_rtl_lowering *lowering,pigen_module_id module,pigen_rtl_module_id rtl_module)
{
	pigen_semantic_model *sem=lowering->semantics;pigen_rtl_model *rtl=lowering->rtl;
	for(size_t i=0;i<sem->generate_scope_count;i++)
	{
		const pigen_semantic_generate_scope *scope=&sem->generate_scopes[i];
		if(scope->module.index!=module.index) continue;
		char *path=source_scope_path(sem,module,scope->scope);
		char prefix[64];snprintf(prefix,sizeof prefix,"__pigen_scope_%u_",scope->scope.index);
		pigen_rtl_source_reference reference={.role=PIGEN_RTL_SOURCE_SCOPE,.module=rtl_module,.scope=scope->scope,
			.signal={PIGEN_INVALID_ID},.object={PIGEN_INVALID_ID},.instance={PIGEN_INVALID_ID},.source_path=path,.emitted_name=prefix};
		int ok=pigen_rtl_source_reference_add(rtl,&reference)!=SIZE_MAX;free(path);if(!ok) return 0;
	}
	for(size_t i=0;i<sem->signal_count;i++)
	{
		if(sem->signals[i].module.index!=module.index || i>=lowering->lowered_endpoint_count) continue;
		pigen_rtl_signal_endpoints endpoints=lowering->lowered_endpoints[i];
		if(endpoints.payload.index==PIGEN_INVALID_ID) continue;
		pigen_signal_id signal={(uint32_t)i};char *path=source_signal_path(sem,signal);
		if(!path) return 0;
		pigen_rtl_object_id objects[]={endpoints.payload,source_control_object(rtl,endpoints.valid),source_control_object(rtl,endpoints.ready),
			endpoints.source_ready,endpoints.input_payload,endpoints.input_valid,endpoints.input_ready};
		pigen_rtl_source_role roles[]={PIGEN_RTL_SOURCE_VALUE,PIGEN_RTL_SOURCE_VALID,PIGEN_RTL_SOURCE_READY,
			PIGEN_RTL_SOURCE_READY,PIGEN_RTL_SOURCE_INPUT_VALUE,PIGEN_RTL_SOURCE_INPUT_VALID,PIGEN_RTL_SOURCE_INPUT_READY};
		int ok=1;
		for(size_t j=0;j<sizeof objects/sizeof *objects && ok;j++) ok=source_object_reference(lowering,rtl_module,signal,roles[j],objects[j],path);
		free(path);if(!ok) return 0;
	}
	for(size_t i=0;i<lowering->protocol_port_count;i++)
	{
		const pigen_rtl_protocol_channel_ports *ports=&lowering->protocol_ports[i];
		const pigen_semantic_protocol_endpoint *endpoint=pigen_protocol_endpoint_get(sem,ports->endpoint);
		if(endpoint->module.index!=module.index) continue;
		pigen_signal_id signal=endpoint->channels[ports->channel];char *path=source_signal_path(sem,signal);
		if(!path) return 0;
		int ok=source_object_reference(lowering,rtl_module,signal,PIGEN_RTL_SOURCE_VALID,ports->valid,path) &&
			source_object_reference(lowering,rtl_module,signal,PIGEN_RTL_SOURCE_READY,ports->ready,path);
		for(size_t f=0;f<PIGEN_FIELD_COUNT && ok;f++) if(ports->fields[f].index!=PIGEN_INVALID_ID)
		{
			pigen_string field={0};pigen_append(&field,path);pigen_append(&field,".");pigen_append(&field,pigen_protocol_field_name((pigen_protocol_field)f));
			ok=source_object_reference(lowering,rtl_module,signal,PIGEN_RTL_SOURCE_FIELD,ports->fields[f],field.data);free(field.data);
		}
		free(path);if(!ok) return 0;
	}
	for(size_t i=0;i<rtl->instance_count;i++)
	{
		const pigen_rtl_instance *instance=&rtl->instances[i];
		if(instance->module.index!=rtl_module.index || !instance->definition) continue;
		const pigen_semantic_instance *source=pigen_instance_get(sem,instance->semantic_instance);
		const pigen_symbol *symbol=source ? pigen_symbol_get(sem,source->symbol) : NULL;
		char *path=source ? source_symbol_path(sem,module,source->symbol) : NULL;
		if(!symbol || !path || !instance->name) {free(path);return 0;}
		pigen_rtl_source_reference reference={.role=PIGEN_RTL_SOURCE_INSTANCE,.module=rtl_module,.scope=symbol->scope,
			.signal={PIGEN_INVALID_ID},.object={PIGEN_INVALID_ID},.instance={(uint32_t)i},.source_path=path,.emitted_name=instance->name};
		int ok=pigen_rtl_source_reference_add(rtl,&reference)!=SIZE_MAX;free(path);if(!ok) return 0;
		for(size_t c=0;c<instance->connection_records.count;c++)
		{
			const pigen_rtl_connection *connection=&rtl->connections[instance->connection_records.first+c];
			path=source_instance_port_path(lowering,source,connection->port);
			pigen_string emitted={0};pigen_append(&emitted,instance->name);pigen_append(&emitted,".");pigen_append(&emitted,connection->port);
			reference.role=PIGEN_RTL_SOURCE_INSTANCE_PORT;reference.source_path=path;reference.emitted_name=emitted.data;
			ok=pigen_rtl_source_reference_add(rtl,&reference)!=SIZE_MAX;free(path);free(emitted.data);if(!ok) return 0;
		}
	}
	return 1;
}

pigen_rtl_module_id pigen_lower_rtl_module(pigen_rtl_lowering *lowering,
	pigen_module_id module)
{
	pigen_rtl_model *rtl;
	pigen_rtl_module_id rtl_module;
	const pigen_rtl_module *record;

	if (!lowering || !lowering->semantics || !lowering->rtl)
		return (pigen_rtl_module_id){PIGEN_INVALID_ID};
	if (module.index == PIGEN_INVALID_ID ||
		!pigen_module_get(lowering->semantics, module))
		return (pigen_rtl_module_id){PIGEN_INVALID_ID};
	if (pigen_lower_rtl_generated_instances(lowering,module)!=0 || lower_specialization_parameters(lowering,module)!=0)
		return (pigen_rtl_module_id){PIGEN_INVALID_ID};
	rtl = lowering->rtl;
	rtl_module = rtl_module_for_semantic(rtl, lowering->semantics, module);
	if (rtl_module.index == PIGEN_INVALID_ID)
		return rtl_module;
	if(!lower_retained_observations(lowering,module,rtl_module) || !lower_source_references(lowering,module,rtl_module)) return (pigen_rtl_module_id){PIGEN_INVALID_ID};
	record = pigen_rtl_module_get(rtl, rtl_module);
	if (!record)
		return (pigen_rtl_module_id){PIGEN_INVALID_ID};
	/* The owner ranges are maintained by the record constructors as records
	 * are appended. Composition publishes the module identity; it does not
	 * re-derive records or require every transfer to own exactly one equation
	 * (a transfer may publish payload, valid and ready wiring equations). */
	return rtl_module;
}
