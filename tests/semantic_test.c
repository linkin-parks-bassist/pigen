#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/semantic.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static pigen_source_span occurrence(pigen_source_id source, const char *text,
	const char *word, size_t ordinal)
{
	const char *at = text;
	size_t i;

	for (i = 0; i <= ordinal; i++)
	{
		at = strstr(at, word);
		assert(at);
		if (i != ordinal) at += strlen(word);
	}
	return (pigen_source_span){source, (size_t)(at - text),
		(size_t)(at - text) + strlen(word)};
}

static void assert_conversion(pigen_conversion conversion,
	pigen_conversion_kind kind, pigen_data_type_id source,
	pigen_data_type_id target)
{
	assert(conversion.kind == kind);
	assert(conversion.source_data_type.index == source.index);
	assert(conversion.target_data_type.index == target.index);
}

static uint64_t evaluate_width(const pigen_semantic_model *model,
	pigen_const_expr_id expression)
{
	const pigen_const_expr *known = pigen_const_expr_get(model, expression);
	const pigen_const_expr_id *children;
	uint64_t result;
	size_t i;

	assert(known);
	if (known->kind == PIGEN_CONST_EXPR_INTEGER) return known->as.integer;
	assert(known->kind == PIGEN_CONST_EXPR_WIDTH_SUM ||
		known->kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT ||
		known->kind == PIGEN_CONST_EXPR_WIDTH_MAXIMUM);
	children = pigen_const_expr_children(model, known->as.sequence.first_child,
		known->as.sequence.child_count);
	assert(children);
	result = known->kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT ? 1 : 0;
	for (i = 0; i < known->as.sequence.child_count; i++)
	{
		uint64_t child = evaluate_width(model, children[i]);

		if (known->kind == PIGEN_CONST_EXPR_WIDTH_SUM) result += child;
		else if (known->kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT) result *= child;
		else if (child > result) result = child;
	}
	return result;
}

static void assert_numerical_width(pigen_semantic_model *model,
	pigen_data_type_id type, pigen_numerical_interpretation interpretation,
	uint64_t width)
{
	assert(pigen_data_type_numerical_interpretation(model, type) ==
		interpretation);
	assert(evaluate_width(model, pigen_data_type_packed_width(model, type)) ==
		width);
}

int main(void)
{
	const char text[] =
		"module sample;\n"
		"  logic [7:0] value;\n"
		"  pipeline pipe begin\n"
		"    logic [7:0] value;\n"
		"    stage first begin value <= value; end\n"
		"    stage second begin value <= value; end\n"
		"    yield value;\n"
		"  endpipeline\n"
		"endmodule\n"
		"bit\n"
		"int\n"
		"uint\n"
		"byte\n"
		"byte_alias_a\n"
		"byte_alias_b\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "scope.pigen", text,
		strlen(text));
	pigen_source_span whole = {source, 0, strlen(text)};
	pigen_source_span first_value = occurrence(source, text, "value", 0);
	pigen_source_span second_value = occurrence(source, text, "value", 1);
	pigen_source_span third_value = occurrence(source, text, "value", 2);
	pigen_source_span range = occurrence(source, text, "[7:0]", 0);
	pigen_semantic_model model;
	pigen_packed_dimension dimension;
	pigen_packed_dimension matrix_dimensions[2];
	pigen_shape_dimension signal_dimensions[2];
	pigen_expr_id left_bound;
	pigen_expr_id right_bound;
	pigen_expr_id conditional;
	pigen_expr_id same_conditional;
	pigen_expr_id module_value_expression;
	pigen_expr_id exact_expression;
	pigen_expr_id converted_expression;
	pigen_expr_id resized_expression;
	pigen_expr_id shaped_concatenation_children[1];
	pigen_expr_id first_shape_index;
	pigen_expr_id second_shape_index;
	pigen_lvalue_id module_value_lvalue;
	pigen_data_type_id unsized_integer_data_type;
	pigen_data_type_id aliased_unsized_integer_type;
	pigen_data_type_id signed_8;
	pigen_data_type_id same_signed_8;
	pigen_data_type_id signed_12;
	pigen_data_type_id signed_16;
	pigen_data_type_id unsigned_8;
	pigen_data_type_id unsigned_12;
	pigen_data_type_id unsigned_2;
	pigen_data_type_id exact_one;
	pigen_data_type_id exact_zero;
	pigen_data_type_id exact_three;
	pigen_integer_id one;
	pigen_integer_id zero;
	pigen_integer_id three;
	pigen_integer_id negative_one;
	pigen_data_type_id pigen_byte;
	pigen_data_type_id byte_alias_a;
	pigen_data_type_id byte_alias_b;
	pigen_data_type_id signed_integer_alias;
	pigen_data_type_id boolean_type;
	pigen_data_type_id byte_type;
	pigen_data_type_id same_byte_type;
	pigen_data_type_id matrix_type;
	pigen_data_type_id row_type;
	pigen_data_type_id element_type;
	pigen_data_type_id selected_type;
	pigen_data_type_id reverse_selected_type;
	pigen_data_type_id indexed_selected_type;
	pigen_data_type_id bit_type;
	pigen_data_type_id sized_logic_type;
	pigen_data_type_id bit_concat_type;
	pigen_data_type_id mixed_concat_type;
	pigen_const_expr_id width_values[3];
	pigen_const_expr_id width_8;
	pigen_const_expr_id width_2;
	pigen_const_expr_id width_12;
	pigen_const_expr_id width_16;
	pigen_const_expr_id width_maximum;
	pigen_const_expr_id reordered_width_maximum;
	pigen_const_expr_id nested_width_maximum;
	pigen_const_expr_id zero_width;
	pigen_const_expr_id symbolic_width;
	pigen_const_expr_id nonintegral_width;
	const pigen_const_expr_id *maximum_children;
	pigen_unary_resolution unary_resolution;
	pigen_binary_resolution binary_resolution;
	pigen_conditional_resolution conditional_resolution;
	pigen_conversion conversion;
	pigen_data_type_argument type_argument;
	pigen_shape_id scalar_shape;
	pigen_shape_id signal_shape;
	pigen_shape_id same_signal_shape;
	pigen_shape_id tail_shape;
	pigen_data_type_id concat_types[2];
	pigen_scope_id module_scope;
	pigen_scope_id pipeline_scope;
	pigen_scope_id first_stage;
	pigen_scope_id second_stage;
	pigen_module_id module;
	pigen_symbol_id module_symbol;
	pigen_symbol_id unsized_integer_alias_symbol;
	pigen_symbol_id signed_integer_alias_symbol;
	pigen_symbol_id byte_alias_a_symbol;
	pigen_symbol_id byte_alias_b_symbol;
	pigen_symbol_id module_value;
	pigen_symbol_id pipeline_value;
	pigen_symbol_id first_local;
	pigen_signal_id module_value_signal;
	pigen_signal_id pipeline_value_signal;
	pigen_signal_id first_local_signal;
	pigen_symbol_id shadowed;
	pigen_symbol_id found;
	const pigen_transfer_type_descriptor *wire_descriptor;
	const pigen_transfer_type_descriptor *logic_descriptor;
	const pigen_transfer_type_descriptor *fifo_descriptor;
	size_t i;

	pigen_semantic_init(&model, &sources);
	scalar_shape = pigen_shape_intern(&model, NULL, 0);
	assert(scalar_shape.index != PIGEN_INVALID_ID);
	assert(scalar_shape.index ==
		pigen_semantic_scalar_shape(&model).index);
	assert(pigen_shape_get(&model, scalar_shape)->dimension_count == 0);
	wire_descriptor = pigen_transfer_type_descriptor_get(
		PIGEN_TRANSFER_TYPE_WIRE);
	logic_descriptor = pigen_transfer_type_descriptor_get(
		PIGEN_TRANSFER_TYPE_LOGIC);
	fifo_descriptor = pigen_transfer_type_descriptor_get(
		PIGEN_TRANSFER_TYPE_FIFO);
	assert(wire_descriptor && wire_descriptor->is_static &&
		wire_descriptor->valid_constant == 1 &&
		wire_descriptor->ready_constant == 0 &&
		!wire_descriptor->consumes_on_read &&
		!wire_descriptor->produces_on_write &&
		!wire_descriptor->requires_ownership && !wire_descriptor->binds_domain);
	assert(logic_descriptor && logic_descriptor->is_static &&
		logic_descriptor->valid_constant == 1 &&
		logic_descriptor->ready_constant == 1 &&
		!logic_descriptor->consumes_on_read &&
		!logic_descriptor->produces_on_write &&
		!logic_descriptor->requires_ownership &&
		!logic_descriptor->binds_domain);
	assert(fifo_descriptor && !fifo_descriptor->is_static &&
		fifo_descriptor->valid_constant < 0 &&
		fifo_descriptor->ready_constant < 0 &&
		fifo_descriptor->consumes_on_read &&
		fifo_descriptor->produces_on_write &&
		fifo_descriptor->requires_ownership && fifo_descriptor->binds_domain);
	unsized_integer_data_type = pigen_data_type_unsized_integer(&model);
	boolean_type = pigen_data_type_boolean(&model);
	assert(unsized_integer_data_type.index ==
		pigen_data_type_unsized_integer(&model).index);
	assert(boolean_type.index ==
		pigen_data_type_boolean(&model).index);
	assert(pigen_data_type_exists(&model, unsized_integer_data_type));
	assert(pigen_data_type_exists(&model, boolean_type));
	assert(pigen_data_type_signedness(&model, unsized_integer_data_type) ==
		PIGEN_SIGN_SIGNED);
	assert(pigen_data_type_signedness(&model, boolean_type) ==
		PIGEN_SIGN_UNSIGNED);
	width_8 = pigen_const_expr_intern_integer(&model, 8,
		unsized_integer_data_type);
	width_2 = pigen_const_expr_intern_integer(&model, 2,
		unsized_integer_data_type);
	width_12 = pigen_const_expr_intern_integer(&model, 12,
		unsized_integer_data_type);
	width_16 = pigen_const_expr_intern_integer(&model, 16,
		unsized_integer_data_type);
	signed_8 = pigen_data_type_signed_integer(&model, width_8);
	same_signed_8 = pigen_data_type_signed_integer(&model, width_8);
	signed_12 = pigen_data_type_signed_integer(&model, width_12);
	signed_16 = pigen_data_type_signed_integer(&model, width_16);
	unsigned_8 = pigen_data_type_unsigned_integer(&model, width_8);
	unsigned_12 = pigen_data_type_unsigned_integer(&model, width_12);
	unsigned_2 = pigen_data_type_unsigned_integer(&model, width_2);
	type_argument = (pigen_data_type_argument){PIGEN_DATA_TYPE_ARGUMENT_COUNT,
		{.count = width_8}};
	assert(pigen_data_type_from_spelling(&model,
		occurrence(source, text, "int", 0), PIGEN_SIGN_IMPLICIT,
		&type_argument, 1).index == signed_8.index);
	assert(pigen_data_type_from_spelling(&model,
		occurrence(source, text, "uint", 0), PIGEN_SIGN_IMPLICIT,
		&type_argument, 1).index == unsigned_8.index);
	zero = pigen_integer_intern_u64(&model, 0);
	one = pigen_integer_intern_u64(&model, 1);
	three = pigen_integer_intern_u64(&model, 3);
	negative_one = pigen_integer_negate(&model, one);
	exact_zero = pigen_data_type_exact_integer(&model, zero);
	exact_one = pigen_data_type_exact_integer(&model, one);
	exact_three = pigen_data_type_exact_integer(&model, three);
	assert(pigen_data_type_exact_value(&model, exact_one).index == one.index);
	assert(pigen_data_type_numerical_interpretation(&model, exact_one) ==
		PIGEN_NUMERICAL_EXACT_INTEGER);
	pigen_byte = pigen_data_type_byte(&model);
	bit_type = pigen_data_type_from_spelling(&model,
		occurrence(source, text, "bit", 0), PIGEN_SIGN_UNSIGNED, NULL, 0);
	assert(pigen_data_type_from_spelling(&model,
		occurrence(source, text, "int", 0), PIGEN_SIGN_IMPLICIT, NULL, 0).index ==
		PIGEN_INVALID_ID);
	assert(pigen_data_type_from_spelling(&model,
		occurrence(source, text, "uint", 0), PIGEN_SIGN_IMPLICIT, NULL, 0).index ==
		PIGEN_INVALID_ID);
	assert(pigen_data_type_from_spelling(&model,
		occurrence(source, text, "byte", 0), PIGEN_SIGN_IMPLICIT, NULL, 0).index ==
		pigen_byte.index);
	assert(signed_8.index == same_signed_8.index);
	assert(signed_8.index != signed_16.index);
	assert(signed_8.index != unsigned_8.index);
	assert(pigen_data_type_numerical_interpretation(&model, signed_8) ==
		PIGEN_NUMERICAL_SIGNED_INTEGER);
	assert(pigen_data_type_numerical_interpretation(&model, unsigned_8) ==
		PIGEN_NUMERICAL_UNSIGNED_INTEGER);
	assert(pigen_data_type_numerical_interpretation(&model, pigen_byte) ==
		PIGEN_NUMERICAL_NONE);
	assert(pigen_data_type_state_domain(&model, signed_8) ==
		PIGEN_DATA_TYPE_STATE_TWO);
	assert(pigen_data_type_state_domain(&model, unsigned_8) ==
		PIGEN_DATA_TYPE_STATE_TWO);
	assert(pigen_data_type_state_domain(&model, pigen_byte) ==
		PIGEN_DATA_TYPE_STATE_TWO);
	assert(pigen_const_expr_get(&model,
		pigen_data_type_packed_width(&model, pigen_byte))->as.integer == 8);
	assert(pigen_data_type_packed_element(&model, signed_8).index ==
		bit_type.index);
	assert(pigen_data_type_packed_element(&model, pigen_byte).index ==
		bit_type.index);
	assert(pigen_data_type_signed_integer(&model,
		INVALID_ID(pigen_const_expr_id)).index == PIGEN_INVALID_ID);
	nonintegral_width = pigen_const_expr_intern_integer(&model, 8, pigen_byte);
	assert(pigen_data_type_signed_integer(&model, nonintegral_width).index ==
		PIGEN_INVALID_ID);
	assert(pigen_data_type_resolve_assignment_conversion(&model,
		signed_8, signed_8, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_IDENTITY);
	assert(conversion.source_data_type.index == signed_8.index);
	assert(conversion.target_data_type.index == signed_8.index);
	assert(pigen_data_type_resolve_assignment_conversion(&model,
		signed_8, signed_16, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_INTEGER_RESIZE);
	assert(conversion.source_data_type.index == signed_8.index);
	assert(conversion.target_data_type.index == signed_16.index);
	conversion = (pigen_conversion){PIGEN_CONVERSION_INVALID,
		INVALID_ID(pigen_data_type_id), INVALID_ID(pigen_data_type_id)};
	assert(!pigen_data_type_resolve_assignment_conversion(&model,
		signed_8, unsigned_8, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_INVALID);
	assert(!pigen_data_type_resolve_assignment_conversion(&model,
		pigen_byte, unsigned_8, &conversion));
	assert(!pigen_data_type_resolve_assignment_conversion(&model,
		INVALID_ID(pigen_data_type_id), signed_8, &conversion));
	assert(!pigen_data_type_resolve_assignment_conversion(&model,
		signed_8, signed_16, NULL));
	assert(pigen_data_type_resolve_explicit_conversion(&model,
		signed_8, unsigned_8, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_INTEGER_REINTERPRET);
	assert(pigen_data_type_resolve_explicit_conversion(&model,
		signed_8, unsigned_12, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_INTEGER_REINTERPRET);
	assert(conversion.source_data_type.index == signed_8.index);
	assert(conversion.target_data_type.index == unsigned_12.index);
	assert(pigen_data_type_resolve_explicit_conversion(&model,
		pigen_byte, unsigned_8, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_VECTOR_TO_INTEGER);
	assert(pigen_data_type_resolve_explicit_conversion(&model,
		pigen_byte, signed_12, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_VECTOR_TO_INTEGER);
	assert(pigen_data_type_resolve_explicit_conversion(&model,
		unsigned_8, pigen_byte, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_INTEGER_TO_VECTOR);
	assert(pigen_data_type_resolve_explicit_conversion(&model,
		unsigned_12, pigen_byte, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_INTEGER_TO_VECTOR);
	assert(!pigen_data_type_resolve_explicit_conversion(&model,
		INVALID_ID(pigen_data_type_id), pigen_byte, &conversion));
	assert(!pigen_data_type_resolve_explicit_conversion(&model,
		pigen_byte, signed_8, NULL));

	/* Pigen numerical operations derive intrinsic, lossless result types. */
	assert(pigen_data_type_resolve_binary_operation(&model, PIGEN_BINARY_ADD,
		unsigned_8, unsigned_8, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_UNSIGNED_INTEGER, 9);
	assert(pigen_data_type_resolve_binary_operation(&model, PIGEN_BINARY_ADD,
		signed_8, signed_8, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 9);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_MULTIPLY, binary_resolution.operation.result_data_type,
		signed_8, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 17);
	assert(pigen_data_type_resolve_binary_operation(&model, PIGEN_BINARY_ADD,
		signed_8, unsigned_8, &binary_resolution));
	assert_conversion(binary_resolution.left_conversion,
		PIGEN_CONVERSION_INTEGER_RESIZE, signed_8,
		binary_resolution.operation.left_data_type);
	assert_conversion(binary_resolution.right_conversion,
		PIGEN_CONVERSION_INTEGER_PROMOTION, unsigned_8,
		binary_resolution.operation.right_data_type);
	assert_numerical_width(&model, binary_resolution.operation.left_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 9);
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 10);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_SUBTRACT, unsigned_8, unsigned_8, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 9);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_MULTIPLY, unsigned_8, unsigned_8, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_UNSIGNED_INTEGER, 16);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_MULTIPLY, signed_8, signed_8, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 16);

	assert(pigen_data_type_resolve_binary_operation(&model, PIGEN_BINARY_ADD,
		unsigned_8, exact_one, &binary_resolution));
	assert_conversion(binary_resolution.right_conversion,
		PIGEN_CONVERSION_EXACT_INTEGER, exact_one,
		binary_resolution.operation.right_data_type);
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_UNSIGNED_INTEGER, 9);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_MULTIPLY, unsigned_8, exact_three, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_UNSIGNED_INTEGER, 10);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_MULTIPLY, unsigned_8, exact_one, &binary_resolution));
	assert(binary_resolution.operation.result_data_type.index ==
		unsigned_8.index);
	assert(pigen_data_type_resolve_binary_operation(&model, PIGEN_BINARY_ADD,
		unsigned_8, exact_zero, &binary_resolution));
	assert(binary_resolution.operation.result_data_type.index ==
		unsigned_8.index);

	assert(pigen_data_type_resolve_unary_operation(&model,
		PIGEN_UNARY_NEGATE, signed_8, &unary_resolution));
	assert_numerical_width(&model, unary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 9);
	assert(pigen_data_type_resolve_unary_operation(&model,
		PIGEN_UNARY_NEGATE, unsigned_8, &unary_resolution));
	assert_numerical_width(&model, unary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 9);
	assert(pigen_data_type_resolve_unary_operation(&model,
		PIGEN_UNARY_NEGATE, exact_one, &unary_resolution));
	assert(pigen_data_type_exact_value(&model,
		unary_resolution.operation.result_data_type).index ==
		negative_one.index);

	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_BITWISE_AND, signed_8, unsigned_8, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 9);
	for (i = PIGEN_BINARY_LESS; i <= PIGEN_BINARY_WILDCARD_NOT_EQUAL; i++)
	{
		assert(pigen_data_type_resolve_binary_operation(&model,
			(pigen_binary_operator)i, signed_8, unsigned_8,
			&binary_resolution));
		assert(binary_resolution.operation.result_data_type.index ==
			boolean_type.index);
	}
	for (i = PIGEN_BINARY_BITWISE_AND; i <= PIGEN_BINARY_BITWISE_OR; i++)
		assert(pigen_data_type_resolve_binary_operation(&model,
			(pigen_binary_operator)i, signed_8, unsigned_8,
			&binary_resolution));
	for (i = PIGEN_BINARY_LOGICAL_AND; i <= PIGEN_BINARY_LOGICAL_OR; i++)
	{
		assert(pigen_data_type_resolve_binary_operation(&model,
			(pigen_binary_operator)i, signed_8, unsigned_8,
			&binary_resolution));
		assert(binary_resolution.operation.result_data_type.index ==
			boolean_type.index);
	}
	assert(pigen_data_type_resolve_unary_operation(&model,
		PIGEN_UNARY_POSITIVE, signed_8, &unary_resolution));
	assert(pigen_data_type_resolve_unary_operation(&model,
		PIGEN_UNARY_BITWISE_NOT, signed_8, &unary_resolution));
	assert(pigen_data_type_resolve_unary_operation(&model,
		PIGEN_UNARY_LOGICAL_NOT, signed_8, &unary_resolution));
	assert(unary_resolution.operation.result_data_type.index ==
		boolean_type.index);
	for (i = PIGEN_UNARY_REDUCTION_AND; i <= PIGEN_UNARY_REDUCTION_XNOR; i++)
	{
		assert(pigen_data_type_resolve_unary_operation(&model,
			(pigen_unary_operator)i, signed_8, &unary_resolution));
		assert(unary_resolution.operation.result_data_type.index ==
			boolean_type.index);
	}

	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_SHIFT_RIGHT, signed_8, exact_one, &binary_resolution));
	assert(binary_resolution.operation.result_data_type.index == signed_8.index);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_ARITH_SHIFT_RIGHT, signed_8, exact_one,
		&binary_resolution));
	assert(binary_resolution.operation.result_data_type.index == signed_8.index);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_SHIFT_LEFT, signed_8, exact_one, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 9);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_ARITH_SHIFT_LEFT, signed_8, exact_one,
		&binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 9);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_POWER, unsigned_8, exact_three, &binary_resolution));
	assert_numerical_width(&model, binary_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_UNSIGNED_INTEGER, 24);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_SHIFT_LEFT, signed_8, unsigned_8, &binary_resolution));
	assert(pigen_const_expr_get(&model, pigen_data_type_packed_width(&model,
		binary_resolution.operation.result_data_type))->kind ==
		PIGEN_CONST_EXPR_WIDTH_SUM);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_POWER, unsigned_8, unsigned_8, &binary_resolution));
	assert(pigen_const_expr_get(&model, pigen_data_type_packed_width(&model,
		binary_resolution.operation.result_data_type))->kind ==
		PIGEN_CONST_EXPR_WIDTH_PRODUCT);
	assert(!pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_DIVIDE, signed_8, signed_8, &binary_resolution));
	assert(!pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_MODULO, signed_8, signed_8, &binary_resolution));
	binary_resolution.operation.operator = PIGEN_BINARY_POWER;
	assert(!pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_DIVIDE, signed_8, signed_8, &binary_resolution));
	assert(binary_resolution.operation.operator == PIGEN_BINARY_POWER);

	assert(pigen_data_type_resolve_conditional_operation(&model, signed_8,
		signed_8, unsigned_8, &conditional_resolution));
	assert_numerical_width(&model,
		conditional_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_SIGNED_INTEGER, 9);
	assert(pigen_data_type_resolve_conditional_operation(&model, signed_8,
		exact_one, exact_three, &conditional_resolution));
	assert_numerical_width(&model,
		conditional_resolution.operation.result_data_type,
		PIGEN_NUMERICAL_UNSIGNED_INTEGER, 2);
	assert(!pigen_data_type_resolve_binary_operation(&model, PIGEN_BINARY_ADD,
		pigen_byte, pigen_byte, &binary_resolution));
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_BITWISE_AND, pigen_byte, pigen_byte,
		&binary_resolution));
	assert(binary_resolution.operation.result_data_type.index ==
		pigen_byte.index);
	assert(pigen_data_type_resolve_unary_operation(&model,
		PIGEN_UNARY_REDUCTION_XOR, pigen_byte, &unary_resolution));
	assert(unary_resolution.operation.result_data_type.index ==
		boolean_type.index);
	assert(pigen_data_type_resolve_assignment_conversion(&model,
		exact_three, unsigned_8, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_EXACT_INTEGER);
	assert(pigen_data_type_resolve_explicit_conversion(&model,
		exact_three, pigen_byte, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_EXACT_INTEGER);
	assert(pigen_data_type_packed_width(&model, exact_three).index ==
		PIGEN_INVALID_ID);
	exact_expression = pigen_expr_add_exact_integer(&model, three, whole);
	assert(pigen_expr_get(&model, exact_expression)->kind ==
		PIGEN_EXPR_EXACT_INTEGER);
	converted_expression = pigen_expr_add_conversion(&model,
		(pigen_conversion){PIGEN_CONVERSION_EXACT_INTEGER, exact_three,
			unsigned_2}, exact_expression, whole);
	resized_expression = pigen_expr_add_conversion(&model,
		(pigen_conversion){PIGEN_CONVERSION_INTEGER_RESIZE, unsigned_2,
			unsigned_8}, converted_expression, whole);
	assert(pigen_expr_get(&model, converted_expression)->kind ==
		PIGEN_EXPR_CONVERSION);
	assert(pigen_expr_get(&model, converted_expression)->data_type.index ==
		unsigned_2.index);
	assert(pigen_expr_get(&model, converted_expression)->shape.index ==
		pigen_expr_get(&model, exact_expression)->shape.index);
	assert(pigen_expr_get(&model, resized_expression)->data_type.index ==
		unsigned_8.index);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, converted_expression))->kind ==
		PIGEN_CONST_EXPR_CONVERSION);
	assert(pigen_lvalue_resolve(&model, converted_expression).index ==
		PIGEN_INVALID_ID);
	{
		size_t expression_count = model.expression_count;
		size_t constant_count = model.constant_expression_count;

		assert(pigen_expr_add_conversion(&model,
			(pigen_conversion){PIGEN_CONVERSION_IDENTITY, unsigned_2,
				unsigned_2}, converted_expression, whole).index ==
			PIGEN_INVALID_ID);
		assert(pigen_expr_add_conversion(&model,
			(pigen_conversion){PIGEN_CONVERSION_INTEGER_RESIZE, unsigned_8,
				unsigned_12}, converted_expression, whole).index ==
			PIGEN_INVALID_ID);
		assert(pigen_expr_add_conversion(&model,
			(pigen_conversion){PIGEN_CONVERSION_INTEGER_RESIZE, unsigned_2,
				INVALID_ID(pigen_data_type_id)}, converted_expression, whole).index ==
			PIGEN_INVALID_ID);
		assert(pigen_expr_add_conversion(&model,
			(pigen_conversion){PIGEN_CONVERSION_INVALID, unsigned_2,
				unsigned_8}, converted_expression, whole).index ==
			PIGEN_INVALID_ID);
		assert(model.expression_count == expression_count);
		assert(model.constant_expression_count == constant_count);
	}
	left_bound = pigen_expr_add_integer(&model, 7,
		unsized_integer_data_type, range);
	right_bound = pigen_expr_add_integer(&model, 0,
		unsized_integer_data_type, range);
	zero_width = pigen_const_expr_intern_integer(&model, 0,
		unsized_integer_data_type);
	assert(pigen_data_type_signed_integer(&model, zero_width).index ==
		PIGEN_INVALID_ID);
	assert(pigen_data_type_numerical_interpretation(&model,
		pigen_data_type_packed_select(&model, signed_8, width_8, width_8,
			PIGEN_SEMANTIC_SELECT_RANGE)) == PIGEN_NUMERICAL_SYSTEMVERILOG);
	assert(pigen_data_type_state_domain(&model,
		pigen_data_type_packed_select(&model, signed_8, width_8, width_8,
			PIGEN_SEMANTIC_SELECT_RANGE)) == PIGEN_DATA_TYPE_STATE_TWO);
	assert(pigen_data_type_numerical_interpretation(&model,
		pigen_data_type_packed_select(&model, pigen_byte, width_8, width_8,
			PIGEN_SEMANTIC_SELECT_RANGE)) == PIGEN_NUMERICAL_SYSTEMVERILOG);
	assert(pigen_data_type_state_domain(&model,
		pigen_data_type_packed_select(&model, pigen_byte, width_8, width_8,
			PIGEN_SEMANTIC_SELECT_RANGE)) == PIGEN_DATA_TYPE_STATE_TWO);
	signal_dimensions[0] = (pigen_shape_dimension){
		PIGEN_SHAPE_DIMENSION_COUNT,
		{.count = pigen_expr_constant(&model, left_bound)}};
	signal_dimensions[1] = (pigen_shape_dimension){
		PIGEN_SHAPE_DIMENSION_RANGE,
		{.range = {pigen_expr_constant(&model, right_bound),
			pigen_expr_constant(&model, left_bound)}}};
	signal_shape = pigen_shape_intern(&model, signal_dimensions, 2);
	tail_shape = pigen_shape_intern(&model, signal_dimensions + 1, 1);
	same_signal_shape = pigen_shape_intern(&model, signal_dimensions, 2);
	assert(signal_shape.index != PIGEN_INVALID_ID);
	assert(signal_shape.index == same_signal_shape.index);
	assert(pigen_shape_get(&model, signal_shape)->dimension_count == 2);
	assert(pigen_shape_dimensions(&model, signal_shape)[0].as.count.index ==
		pigen_expr_constant(&model, left_bound).index);
	assert(pigen_shape_dimensions(&model, signal_shape)[1].as.range.left.index ==
		pigen_expr_constant(&model, right_bound).index);
	assert(pigen_shape_dimensions(&model, signal_shape)[1].as.range.right.index ==
		pigen_expr_constant(&model, left_bound).index);
	assert(pigen_expr_get(&model, left_bound)->shape.index == scalar_shape.index);
	assert(pigen_data_type_resolve_conditional_operation(&model,
		unsized_integer_data_type, unsized_integer_data_type,
		unsized_integer_data_type, &conditional_resolution));
	assert_conversion(conditional_resolution.condition_conversion,
		PIGEN_CONVERSION_IDENTITY, unsized_integer_data_type,
		unsized_integer_data_type);
	assert_conversion(conditional_resolution.when_true_conversion,
		PIGEN_CONVERSION_IDENTITY, unsized_integer_data_type,
		unsized_integer_data_type);
	assert_conversion(conditional_resolution.when_false_conversion,
		PIGEN_CONVERSION_IDENTITY, unsized_integer_data_type,
		unsized_integer_data_type);
	conditional = pigen_expr_add_conditional(&model,
		conditional_resolution.operation,
		right_bound, left_bound, right_bound, range);
	same_conditional = pigen_expr_add_conditional(&model,
		conditional_resolution.operation, right_bound, left_bound, right_bound,
		range);
	assert(conditional.index != same_conditional.index);
	assert(pigen_expr_constant(&model, conditional).index ==
		pigen_expr_constant(&model, same_conditional).index);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, conditional))->kind ==
		PIGEN_CONST_EXPR_CONDITIONAL);
	dimension = (pigen_packed_dimension){
		pigen_expr_constant(&model, left_bound),
		pigen_expr_constant(&model, right_bound)};
	byte_type = pigen_data_type_implicit(&model, PIGEN_SIGN_UNSIGNED,
		&dimension, 1);
	same_byte_type = pigen_data_type_implicit(&model, PIGEN_SIGN_UNSIGNED,
		&dimension, 1);
	assert(byte_type.index != PIGEN_INVALID_ID);
	assert(byte_type.index == same_byte_type.index);
	assert(pigen_data_type_dimension_count(&model, byte_type) == 1);
	assert(pigen_data_type_dimensions(&model, byte_type)->left.index ==
		dimension.left.index);
	matrix_dimensions[0] = dimension;
	matrix_dimensions[1] = dimension;
	matrix_type = pigen_data_type_implicit(&model, PIGEN_SIGN_SIGNED,
		matrix_dimensions, 2);
	row_type = pigen_data_type_packed_element(&model, matrix_type);
	element_type = pigen_data_type_packed_element(&model, row_type);
	assert(pigen_data_type_signedness(&model, row_type) ==
		PIGEN_SIGN_UNSIGNED);
	assert(pigen_data_type_dimension_count(&model, row_type) == 1);
	assert(pigen_data_type_signedness(&model, element_type) ==
		PIGEN_SIGN_UNSIGNED);
	assert(pigen_data_type_dimension_count(&model, element_type) == 0);
	assert(pigen_data_type_packed_element(&model, element_type).index ==
		PIGEN_INVALID_ID);
	assert(pigen_data_type_packed_element(&model,
		unsized_integer_data_type).index == boolean_type.index);
	selected_type = pigen_data_type_packed_select(&model, matrix_type,
		pigen_expr_constant(&model, left_bound),
		pigen_expr_constant(&model, right_bound),
		PIGEN_SEMANTIC_SELECT_RANGE);
	assert(pigen_data_type_signedness(&model, selected_type) ==
		PIGEN_SIGN_UNSIGNED);
	assert(pigen_data_type_dimension_count(&model, selected_type) == 2);
	reverse_selected_type = pigen_data_type_packed_select(&model, matrix_type,
		pigen_expr_constant(&model, right_bound),
		pigen_expr_constant(&model, left_bound),
		PIGEN_SEMANTIC_SELECT_RANGE);
	assert(reverse_selected_type.index == selected_type.index);
	{
		const pigen_packed_dimension *selected_dimensions =
			pigen_data_type_dimensions(&model, selected_type);
		const pigen_const_expr *upper =
			pigen_const_expr_get(&model, selected_dimensions[0].left);
		const pigen_const_expr *width;
		assert(upper && upper->kind == PIGEN_CONST_EXPR_BINARY &&
			upper->as.binary.operation.operator == PIGEN_BINARY_SUBTRACT);
		width = pigen_const_expr_get(&model, upper->as.binary.left);
		assert(width && width->kind == PIGEN_CONST_EXPR_SELECT_WIDTH &&
			width->as.select_width.kind == PIGEN_SEMANTIC_SELECT_RANGE &&
			width->as.select_width.left.index ==
				pigen_expr_constant(&model, left_bound).index &&
			width->as.select_width.right.index ==
				pigen_expr_constant(&model, right_bound).index);
		assert(selected_dimensions[1].left.index == dimension.left.index);
		assert(selected_dimensions[1].right.index == dimension.right.index);
	}
	indexed_selected_type = pigen_data_type_packed_select(&model, matrix_type,
		INVALID_ID(pigen_const_expr_id),
		pigen_expr_constant(&model, left_bound),
		PIGEN_SEMANTIC_SELECT_INDEXED_DOWN);
	assert(pigen_data_type_dimension_count(&model, indexed_selected_type) == 2);
	assert(pigen_data_type_packed_select(&model, matrix_type,
		INVALID_ID(pigen_const_expr_id),
		pigen_expr_constant(&model, left_bound),
		PIGEN_SEMANTIC_SELECT_INDEXED_UP).index ==
		indexed_selected_type.index);
	assert(pigen_data_type_packed_select(&model, element_type,
		pigen_expr_constant(&model, left_bound),
		pigen_expr_constant(&model, right_bound),
		PIGEN_SEMANTIC_SELECT_RANGE).index == PIGEN_INVALID_ID);
	assert(pigen_data_type_dimension_count(&model,
		pigen_data_type_packed_select(&model, unsized_integer_data_type,
			pigen_expr_constant(&model, left_bound),
			pigen_expr_constant(&model, right_bound),
			PIGEN_SEMANTIC_SELECT_RANGE)) == 1);
	bit_type = pigen_data_type_from_spelling(&model,
		occurrence(source, text, "bit", 0), PIGEN_SIGN_UNSIGNED, NULL, 0);
	assert(pigen_data_type_state_domain(&model, bit_type) ==
		PIGEN_DATA_TYPE_STATE_TWO);
	concat_types[0] = bit_type;
	concat_types[1] = bit_type;
	bit_concat_type = pigen_data_type_concatenation(&model, concat_types, 2);
	assert(pigen_data_type_state_domain(&model, bit_concat_type) ==
		PIGEN_DATA_TYPE_STATE_TWO);
	assert(pigen_data_type_signedness(&model, bit_concat_type) ==
		PIGEN_SIGN_UNSIGNED);
	assert(pigen_data_type_dimension_count(&model, bit_concat_type) == 1);
	concat_types[1] = boolean_type;
	mixed_concat_type = pigen_data_type_concatenation(&model, concat_types, 2);
	assert(pigen_data_type_state_domain(&model, mixed_concat_type) ==
		PIGEN_DATA_TYPE_STATE_FOUR);
	concat_types[0] = pigen_byte;
	concat_types[1] = pigen_byte;
	mixed_concat_type = pigen_data_type_concatenation(&model, concat_types, 2);
	assert(pigen_data_type_numerical_interpretation(&model, mixed_concat_type) ==
		PIGEN_NUMERICAL_SYSTEMVERILOG);
	{
		const pigen_packed_dimension *byte_concat_dimension =
			pigen_data_type_dimensions(&model, mixed_concat_type);
		const pigen_const_expr *byte_concat_upper;
		const pigen_const_expr *byte_concat_sum;
		const pigen_const_expr_id *byte_concat_terms;

		assert(pigen_data_type_dimension_count(&model, mixed_concat_type) == 1);
		assert(pigen_const_expr_get(&model,
			pigen_data_type_packed_width(&model, mixed_concat_type))->kind ==
			PIGEN_CONST_EXPR_SELECT_WIDTH);
		byte_concat_upper = pigen_const_expr_get(&model,
			byte_concat_dimension->left);
		assert(byte_concat_upper &&
			byte_concat_upper->kind == PIGEN_CONST_EXPR_BINARY &&
			byte_concat_upper->as.binary.operation.operator ==
				PIGEN_BINARY_SUBTRACT);
		byte_concat_sum = pigen_const_expr_get(&model,
			byte_concat_upper->as.binary.left);
		assert(byte_concat_sum &&
			byte_concat_sum->kind == PIGEN_CONST_EXPR_WIDTH_SUM &&
			byte_concat_sum->as.sequence.child_count == 2);
		byte_concat_terms = pigen_const_expr_children(&model,
			byte_concat_sum->as.sequence.first_child,
			byte_concat_sum->as.sequence.child_count);
		assert(byte_concat_terms && byte_concat_terms[0].index == width_8.index &&
			byte_concat_terms[1].index == width_8.index);
	}
	assert(pigen_data_type_packed_width(&model, matrix_type).index !=
		PIGEN_INVALID_ID);
	assert(pigen_const_expr_get(&model,
		pigen_data_type_packed_width(&model, matrix_type))->kind ==
		PIGEN_CONST_EXPR_WIDTH_PRODUCT);
	width_values[0] = pigen_const_expr_intern_integer(&model, 8,
		unsized_integer_data_type);
	width_values[1] = pigen_const_expr_intern_integer(&model, 16,
		unsized_integer_data_type);
	width_values[2] = width_values[0];
	width_maximum = pigen_const_expr_intern_width_maximum(&model,
		width_values, 3);
	assert(pigen_const_expr_get(&model, width_maximum)->kind ==
		PIGEN_CONST_EXPR_INTEGER);
	assert(pigen_const_expr_get(&model, width_maximum)->as.integer == 16);
	assert(pigen_const_expr_intern_width_maximum(&model,
		width_values, 1).index == width_values[0].index);
	symbolic_width = pigen_const_expr_intern_select_width(&model,
		pigen_expr_constant(&model, left_bound),
		pigen_expr_constant(&model, right_bound),
		PIGEN_SEMANTIC_SELECT_RANGE);
	assert(pigen_data_type_signed_integer(&model, symbolic_width).index !=
		PIGEN_INVALID_ID);
	width_values[0] = symbolic_width;
	width_values[1] = pigen_const_expr_intern_integer(&model, 8,
		unsized_integer_data_type);
	reordered_width_maximum = pigen_const_expr_intern_width_maximum(&model,
		width_values, 2);
	width_values[0] = pigen_const_expr_intern_integer(&model, 8,
		unsized_integer_data_type);
	width_values[1] = symbolic_width;
	assert(reordered_width_maximum.index ==
		pigen_const_expr_intern_width_maximum(&model, width_values, 2).index);
	assert(pigen_const_expr_intern_width_maximum(&model,
		&symbolic_width, 1).index == symbolic_width.index);
	nested_width_maximum = pigen_const_expr_intern_width_maximum(&model,
		width_values, 2);
	width_values[0] = nested_width_maximum;
	width_values[1] = symbolic_width;
	width_values[2] = pigen_const_expr_intern_integer(&model, 8,
		unsized_integer_data_type);
	width_maximum = pigen_const_expr_intern_width_maximum(&model,
		width_values, 3);
	assert(pigen_const_expr_get(&model, width_maximum)->kind ==
		PIGEN_CONST_EXPR_WIDTH_MAXIMUM);
	maximum_children = pigen_const_expr_children(&model,
		pigen_const_expr_get(&model, width_maximum)->as.sequence.first_child,
		pigen_const_expr_get(&model, width_maximum)->as.sequence.child_count);
	assert(pigen_const_expr_get(&model, width_maximum)->as.sequence.child_count == 2);
	assert(maximum_children[0].index == symbolic_width.index ||
		maximum_children[1].index == symbolic_width.index);
	assert(maximum_children[0].index == width_values[2].index ||
		maximum_children[1].index == width_values[2].index);
	zero_width = pigen_const_expr_intern_integer(&model, 0,
		unsized_integer_data_type);
	assert(pigen_const_expr_intern_width_maximum(&model,
		&zero_width, 1).index == zero_width.index);
	assert(pigen_const_expr_intern_width_maximum(&model,
		(pigen_const_expr_id[]){zero_width, symbolic_width}, 2).index ==
		symbolic_width.index);
	assert(pigen_const_expr_intern_width_maximum(&model, NULL, 0).index ==
		PIGEN_INVALID_ID);
	assert(pigen_const_expr_intern_width_maximum(&model,
		(pigen_const_expr_id[]){INVALID_ID(pigen_const_expr_id)}, 1).index ==
		PIGEN_INVALID_ID);

	model.compilation_scope = pigen_scope_add(&model,
		INVALID_ID(pigen_scope_id),
		(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0});
	assert(model.compilation_scope.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_declare(&model, model.compilation_scope,
		PIGEN_SYMBOL_MODULE, INVALID_ID(pigen_data_type_id),
		occurrence(source, text, "sample", 0), whole, &module_symbol,
		NULL) == PIGEN_DECLARE_OK);
	assert(pigen_symbol_declare(&model, model.compilation_scope,
		PIGEN_SYMBOL_TYPEDEF, unsized_integer_data_type,
		occurrence(source, text, "pipe", 0), whole,
		&unsized_integer_alias_symbol, NULL) == PIGEN_DECLARE_OK);
	aliased_unsized_integer_type = pigen_data_type_alias(&model,
		unsized_integer_alias_symbol,
		unsized_integer_data_type, PIGEN_SIGN_IMPLICIT, NULL, 0);
	assert(pigen_symbol_declare(&model, model.compilation_scope,
		PIGEN_SYMBOL_TYPEDEF, signed_8, occurrence(source, text, "bit", 0),
		whole, &signed_integer_alias_symbol, NULL) == PIGEN_DECLARE_OK);
	signed_integer_alias = pigen_data_type_alias(&model,
		signed_integer_alias_symbol, signed_8, PIGEN_SIGN_IMPLICIT, NULL, 0);
	assert(pigen_symbol_declare(&model, model.compilation_scope,
		PIGEN_SYMBOL_TYPEDEF, pigen_byte,
		occurrence(source, text, "byte_alias_a", 0), whole,
		&byte_alias_a_symbol, NULL) == PIGEN_DECLARE_OK);
	byte_alias_a = pigen_data_type_alias(&model, byte_alias_a_symbol,
		pigen_byte, PIGEN_SIGN_IMPLICIT, NULL, 0);
	assert(pigen_symbol_declare(&model, model.compilation_scope,
		PIGEN_SYMBOL_TYPEDEF, pigen_byte,
		occurrence(source, text, "byte_alias_b", 0), whole,
		&byte_alias_b_symbol, NULL) == PIGEN_DECLARE_OK);
	byte_alias_b = pigen_data_type_alias(&model, byte_alias_b_symbol,
		pigen_byte, PIGEN_SIGN_IMPLICIT, NULL, 0);
	assert(aliased_unsized_integer_type.index != PIGEN_INVALID_ID);
	assert(signed_integer_alias.index != PIGEN_INVALID_ID);
	assert(byte_alias_a.index != PIGEN_INVALID_ID);
	assert(byte_alias_b.index != PIGEN_INVALID_ID);
	assert(byte_alias_a.index != byte_alias_b.index);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_BITWISE_AND, byte_alias_a, byte_alias_a, &binary_resolution));
	assert_conversion(binary_resolution.left_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert_conversion(binary_resolution.right_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert(binary_resolution.operation.result_data_type.index ==
		byte_alias_a.index);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_BITWISE_AND, byte_alias_a, byte_alias_b, &binary_resolution));
	assert_conversion(binary_resolution.left_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert_conversion(binary_resolution.right_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_b, byte_alias_b);
	assert(binary_resolution.operation.left_data_type.index ==
		byte_alias_a.index);
	assert(binary_resolution.operation.right_data_type.index ==
		byte_alias_b.index);
	assert(binary_resolution.operation.result_data_type.index == pigen_byte.index);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_BITWISE_OR, byte_alias_a, pigen_byte, &binary_resolution));
	assert_conversion(binary_resolution.left_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert_conversion(binary_resolution.right_conversion,
		PIGEN_CONVERSION_IDENTITY, pigen_byte, pigen_byte);
	assert(binary_resolution.operation.result_data_type.index == pigen_byte.index);
	assert(pigen_data_type_resolve_binary_operation(&model, PIGEN_BINARY_EQUAL,
		byte_alias_a, byte_alias_b, &binary_resolution));
	assert_conversion(binary_resolution.left_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert_conversion(binary_resolution.right_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_b, byte_alias_b);
	assert(binary_resolution.operation.result_data_type.index ==
		boolean_type.index);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_LOGICAL_OR, byte_alias_a, pigen_byte, &binary_resolution));
	assert_conversion(binary_resolution.left_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert_conversion(binary_resolution.right_conversion,
		PIGEN_CONVERSION_IDENTITY, pigen_byte, pigen_byte);
	assert(binary_resolution.operation.result_data_type.index ==
		boolean_type.index);
	assert(pigen_data_type_resolve_conditional_operation(&model, signed_8,
		byte_alias_a, byte_alias_a, &conditional_resolution));
	assert_conversion(conditional_resolution.when_true_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert_conversion(conditional_resolution.when_false_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert(conditional_resolution.operation.result_data_type.index ==
		byte_alias_a.index);
	assert(pigen_data_type_resolve_conditional_operation(&model, signed_8,
		byte_alias_a, byte_alias_b, &conditional_resolution));
	assert_conversion(conditional_resolution.when_true_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert_conversion(conditional_resolution.when_false_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_b, byte_alias_b);
	assert(conditional_resolution.operation.when_true_data_type.index ==
		byte_alias_a.index);
	assert(conditional_resolution.operation.when_false_data_type.index ==
		byte_alias_b.index);
	assert(conditional_resolution.operation.result_data_type.index ==
		pigen_byte.index);
	assert(pigen_data_type_resolve_conditional_operation(&model, signed_8,
		byte_alias_a, pigen_byte, &conditional_resolution));
	assert_conversion(conditional_resolution.when_true_conversion,
		PIGEN_CONVERSION_IDENTITY, byte_alias_a, byte_alias_a);
	assert_conversion(conditional_resolution.when_false_conversion,
		PIGEN_CONVERSION_IDENTITY, pigen_byte, pigen_byte);
	assert(conditional_resolution.operation.result_data_type.index ==
		pigen_byte.index);
	assert(pigen_data_type_alias_target(&model,
		aliased_unsized_integer_type).index ==
		unsized_integer_data_type.index);
	assert(pigen_data_type_is_integral(&model, aliased_unsized_integer_type));
	assert(pigen_data_type_numerical_interpretation(&model,
		signed_integer_alias) == PIGEN_NUMERICAL_SIGNED_INTEGER);
	assert(pigen_data_type_packed_width(&model, signed_integer_alias).index ==
		width_8.index);
	assert(pigen_data_type_resolve_assignment_conversion(&model,
		signed_integer_alias, signed_16, &conversion));
	assert(conversion.kind == PIGEN_CONVERSION_INTEGER_RESIZE);
	assert(conversion.source_data_type.index == signed_integer_alias.index);
	assert(conversion.target_data_type.index == signed_16.index);
	assert(pigen_data_type_resolve_unary_operation(&model,
		PIGEN_UNARY_NEGATE, aliased_unsized_integer_type, &unary_resolution));
	assert_conversion(unary_resolution.operand_conversion,
		PIGEN_CONVERSION_IDENTITY, aliased_unsized_integer_type,
		aliased_unsized_integer_type);
	assert(unary_resolution.operation.operator == PIGEN_UNARY_NEGATE);
	assert(unary_resolution.operation.operand_data_type.index ==
		aliased_unsized_integer_type.index);
	assert(unary_resolution.operation.result_data_type.index ==
		aliased_unsized_integer_type.index);
	assert(pigen_expr_add_unary(&model, unary_resolution.operation, left_bound,
		range).index == PIGEN_INVALID_ID);
	assert(pigen_data_type_resolve_unary_operation(&model,
		PIGEN_UNARY_LOGICAL_NOT, aliased_unsized_integer_type, &unary_resolution));
	assert_conversion(unary_resolution.operand_conversion,
		PIGEN_CONVERSION_IDENTITY, aliased_unsized_integer_type,
		aliased_unsized_integer_type);
	assert(unary_resolution.operation.result_data_type.index ==
		boolean_type.index);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_MULTIPLY, aliased_unsized_integer_type,
		aliased_unsized_integer_type, &binary_resolution));
	assert_conversion(binary_resolution.left_conversion,
		PIGEN_CONVERSION_IDENTITY, aliased_unsized_integer_type,
		aliased_unsized_integer_type);
	assert_conversion(binary_resolution.right_conversion,
		PIGEN_CONVERSION_IDENTITY, aliased_unsized_integer_type,
		aliased_unsized_integer_type);
	assert(binary_resolution.operation.operator == PIGEN_BINARY_MULTIPLY);
	assert(binary_resolution.operation.left_data_type.index ==
		aliased_unsized_integer_type.index);
	assert(binary_resolution.operation.right_data_type.index ==
		aliased_unsized_integer_type.index);
	assert(binary_resolution.operation.result_data_type.index ==
		aliased_unsized_integer_type.index);
	assert(pigen_data_type_resolve_binary_operation(&model,
		PIGEN_BINARY_EQUAL, aliased_unsized_integer_type,
		unsized_integer_data_type, &binary_resolution));
	assert_conversion(binary_resolution.left_conversion,
		PIGEN_CONVERSION_IDENTITY, aliased_unsized_integer_type,
		aliased_unsized_integer_type);
	assert_conversion(binary_resolution.right_conversion,
		PIGEN_CONVERSION_IDENTITY, unsized_integer_data_type,
		unsized_integer_data_type);
	assert(binary_resolution.operation.result_data_type.index ==
		boolean_type.index);
	assert(pigen_data_type_resolve_conditional_operation(&model,
		aliased_unsized_integer_type, aliased_unsized_integer_type,
		aliased_unsized_integer_type, &conditional_resolution));
	assert_conversion(conditional_resolution.condition_conversion,
		PIGEN_CONVERSION_IDENTITY, aliased_unsized_integer_type,
		aliased_unsized_integer_type);
	assert_conversion(conditional_resolution.when_true_conversion,
		PIGEN_CONVERSION_IDENTITY, aliased_unsized_integer_type,
		aliased_unsized_integer_type);
	assert_conversion(conditional_resolution.when_false_conversion,
		PIGEN_CONVERSION_IDENTITY, aliased_unsized_integer_type,
		aliased_unsized_integer_type);
	assert(conditional_resolution.operation.condition_data_type.index ==
		aliased_unsized_integer_type.index);
	assert(conditional_resolution.operation.when_true_data_type.index ==
		aliased_unsized_integer_type.index);
	assert(conditional_resolution.operation.when_false_data_type.index ==
		aliased_unsized_integer_type.index);
	assert(conditional_resolution.operation.result_data_type.index ==
		aliased_unsized_integer_type.index);
	assert(!pigen_data_type_is_integral(&model,
		INVALID_ID(pigen_data_type_id)));
	assert(pigen_data_type_state_domain(&model, unsized_integer_data_type) ==
		PIGEN_DATA_TYPE_STATE_FOUR);
	assert(pigen_data_type_state_domain(&model,
		aliased_unsized_integer_type) ==
		PIGEN_DATA_TYPE_STATE_FOUR);
	sized_logic_type = pigen_data_type_sized_logic(&model, 8,
		PIGEN_SIGN_SIGNED);
	assert(sized_logic_type.index != PIGEN_INVALID_ID);
	assert(pigen_data_type_signedness(&model, sized_logic_type) ==
		PIGEN_SIGN_SIGNED);
	assert(pigen_data_type_dimension_count(&model, sized_logic_type) == 1);
	module_scope = pigen_scope_add(&model, model.compilation_scope, whole);
	module = pigen_module_add(&model, (pigen_syntax_id){0}, module_symbol,
		module_scope, whole);
	pipeline_scope = pigen_scope_add(&model, module_scope, whole);
	first_stage = pigen_scope_add(&model, pipeline_scope, whole);
	second_stage = pigen_scope_add(&model, pipeline_scope, whole);
	assert(module_scope.index != PIGEN_INVALID_ID);
	assert(pigen_scope_add(&model, (pigen_scope_id){9999}, whole).index ==
		PIGEN_INVALID_ID);

	assert(pigen_symbol_declare(&model, module_scope, PIGEN_SYMBOL_SIGNAL,
		byte_type, first_value, whole, &module_value, &shadowed) == PIGEN_DECLARE_OK);
	assert(shadowed.index == PIGEN_INVALID_ID);
	module_value_signal = pigen_signal_add(&model, (pigen_syntax_id){1},
		module, module_value, byte_type, signal_shape,
		INVALID_ID(pigen_expr_id),
		PIGEN_TRANSFER_TYPE_LOGIC, PIGEN_SEMANTIC_INTERNAL, whole);
	assert(module_value_signal.index != PIGEN_INVALID_ID);
	assert(pigen_signal_get(&model, module_value_signal)->shape.index ==
		signal_shape.index);
	module_value_expression = pigen_expr_add_symbol(&model, module_value,
		byte_type, first_value);
	assert(pigen_expr_get(&model, module_value_expression)->shape.index ==
		signal_shape.index);
	shaped_concatenation_children[0] = module_value_expression;
	assert(pigen_expr_add_concatenation(&model,
		shaped_concatenation_children, 1, first_value).index ==
		PIGEN_INVALID_ID);
	first_shape_index = pigen_expr_add_index(&model, module_value_expression,
		right_bound, first_value);
	assert(pigen_expr_get(&model, first_shape_index)->data_type.index ==
		byte_type.index);
	assert(pigen_expr_get(&model, first_shape_index)->shape.index ==
		tail_shape.index);
	second_shape_index = pigen_expr_add_index(&model, first_shape_index,
		right_bound, first_value);
	assert(pigen_expr_get(&model, second_shape_index)->data_type.index ==
		byte_type.index);
	assert(pigen_expr_get(&model, second_shape_index)->shape.index ==
		scalar_shape.index);
	assert(pigen_expr_add_select(&model, module_value_expression, left_bound,
		right_bound, PIGEN_SEMANTIC_SELECT_RANGE, first_value).index ==
		PIGEN_INVALID_ID);
	module_value_lvalue = pigen_lvalue_resolve(&model, module_value_expression);
	assert(module_value_lvalue.index != PIGEN_INVALID_ID);
	assert(pigen_lvalue_get(&model, module_value_lvalue)->kind ==
		PIGEN_LVALUE_PROJECTION);
	assert(pigen_lvalue_get(&model,
		module_value_lvalue)->as.projection.base_symbol.index ==
		module_value.index);
	assert(pigen_lvalue_get(&model,
		module_value_lvalue)->as.projection.signal.index ==
		module_value_signal.index);
	assert(pigen_symbol_declare(&model, pipeline_scope, PIGEN_SYMBOL_SIGNAL,
		byte_type, second_value, whole, &pipeline_value, &shadowed) == PIGEN_DECLARE_OK);
	assert(shadowed.index == module_value.index);
	pipeline_value_signal = pigen_signal_add(&model, (pigen_syntax_id){2},
		module, pipeline_value, byte_type, scalar_shape,
		INVALID_ID(pigen_expr_id),
		PIGEN_TRANSFER_TYPE_LOGIC, PIGEN_SEMANTIC_INTERNAL, whole);
	assert(pipeline_value_signal.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_declare(&model, first_stage, PIGEN_SYMBOL_SIGNAL,
		byte_type, third_value, whole, &first_local, &shadowed) == PIGEN_DECLARE_OK);
	assert(shadowed.index == pipeline_value.index);
	first_local_signal = pigen_signal_add(&model, (pigen_syntax_id){3},
		module, first_local, byte_type, scalar_shape,
		INVALID_ID(pigen_expr_id),
		PIGEN_TRANSFER_TYPE_LOGIC, PIGEN_SEMANTIC_INTERNAL, whole);
	assert(first_local_signal.index != PIGEN_INVALID_ID);

	found = pigen_symbol_lookup(&model, first_stage, first_value);
	assert(found.index == first_local.index);
	found = pigen_symbol_lookup(&model, second_stage, first_value);
	assert(found.index == pipeline_value.index);
	assert(pigen_symbol_declare(&model, pipeline_scope, PIGEN_SYMBOL_SIGNAL,
		byte_type, third_value, whole, &found, NULL) == PIGEN_DECLARE_DUPLICATE);
	assert(found.index == pipeline_value.index);
	assert(model.symbol_count == 8);
	assert(model.signal_count == 3);

	for (i = 0; i < 64; i++)
	{
		pigen_expr_id distinct_left = pigen_expr_add_integer(&model,
			(uint64_t)i + 2, unsized_integer_data_type, range);
		pigen_expr_id distinct_right = pigen_expr_add_integer(&model,
			(uint64_t)i + 66, unsized_integer_data_type, range);
		pigen_packed_dimension distinct_dimension = {
			pigen_expr_constant(&model, distinct_left),
			pigen_expr_constant(&model, distinct_right)};
		pigen_scope_id child = pigen_scope_add(&model, module_scope, whole);
		pigen_data_type_id distinct_type = pigen_data_type_implicit(&model,
			PIGEN_SIGN_UNSIGNED, &distinct_dimension, 1);
		assert(child.index != PIGEN_INVALID_ID);
		assert(distinct_type.index != PIGEN_INVALID_ID);
		assert(pigen_symbol_declare(&model, child, PIGEN_SYMBOL_TYPEDEF,
			distinct_type, first_value, whole, NULL, NULL) == PIGEN_DECLARE_OK);
	}
	assert(pigen_data_type_dimension_count(&model, byte_type) == 1);
	assert(pigen_symbol_get(&model, module_value)->scope.index == module_scope.index);
	assert(pigen_symbol_lookup(&model, first_stage, second_value).index ==
		first_local.index);

	assert(pigen_symbol_declare(&model, module_scope, PIGEN_SYMBOL_SIGNAL,
		(pigen_data_type_id){9999}, first_value, whole, NULL, NULL) ==
		PIGEN_DECLARE_INVALID);
	pigen_free_semantic_model(&model);
	pigen_free_sources(&sources);
	puts("PASS: scopes resolve stable symbols with structured types");
	return 0;
}
