#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/resolve.h"

static int span_is(const pigen_source_manager *sources, pigen_source_span span,
	const char *expected)
{
	size_t length;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static const pigen_semantic_transport *find_transport(
	const pigen_source_manager *sources, const pigen_semantic_model *model,
	const char *name)
{
	size_t i;
	for (i = 0; i < model->transport_count; i++)
	{
		const pigen_symbol *symbol = pigen_symbol_get(model,
			model->transports[i].symbol);
		if (span_is(sources, symbol->name, name)) return &model->transports[i];
	}
	return NULL;
}

static const pigen_semantic_parameter *find_parameter(
	const pigen_source_manager *sources, const pigen_semantic_model *model,
	const char *name)
{
	size_t i;
	for (i = 0; i < model->parameter_count; i++)
	{
		const pigen_semantic_parameter *parameter = pigen_parameter_get(model,
			(pigen_parameter_id){(uint32_t)i});
		const pigen_symbol *symbol = pigen_symbol_get(model,
			parameter->symbol);
		if (span_is(sources, symbol->name, name)) return parameter;
	}
	return NULL;
}

int main(void)
{
	const char text[] =
		"typedef logic [31:0] word_t;\n"
		"module first #(parameter WIDTH = 8, DEPTH = WIDTH / 2, ENABLED = DEPTH >= 4 && !(WIDTH == 0), SELECTED = ENABLED ? DEPTH : WIDTH, HEX = 8'hff, BINARY = 8'b1111_1111, SIGNED_VALUE = 18'sd8192, UNKNOWN_VALUE = 12'hx3z, MASK_DEPTH = HEX == BINARY ? 8'd4 : 8'd2, WIDE_HEX = 80'hffff_ffff_ffff_ffff_ffff, WIDE_DECIMAL = 80'd1208925819614629174706175);\n"
		"  localparam LAST = WIDTH - 1;\n"
		"  localparam NONZERO = |WIDTH;\n"
		"  typedef logic unsigned [LAST:NONZERO] byte_t;\n"
		"  buf byte_t left, right;\n"
		"  fifo word_t[MASK_DEPTH] queue;\n"
		"endmodule\n"
		"module second;\n"
		"  port bit [0:0] pulse;\n"
		"endmodule\n";
	const char duplicate[] =
		"module same; endmodule module same; endmodule\n";
	const char unknown[] =
		"module unknown; buf missing_t value; endmodule\n";
	const char inout[] =
		"module bidirectional; inout buf [7:0] value; endmodule\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "resolve.pigen", text,
		strlen(text));
	pigen_source_id duplicate_source = pigen_source_add(&sources, "duplicate.pigen",
		duplicate, strlen(duplicate));
	pigen_source_id unknown_source = pigen_source_add(&sources, "unknown.pigen",
		unknown, strlen(unknown));
	pigen_source_id inout_source = pigen_source_add(&sources, "inout.pigen", inout,
		strlen(inout));
	pigen_syntax_tree syntax = {0};
	pigen_syntax_tree duplicate_syntax = {0};
	pigen_syntax_tree unknown_syntax = {0};
	pigen_syntax_tree inout_syntax = {0};
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_result duplicate_preprocessed = {0};
	pigen_preprocess_result unknown_preprocessed = {0};
	pigen_preprocess_result inout_preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_error syntax_error = {0};
	pigen_semantic_model model;
	pigen_semantic_model duplicate_model;
	pigen_semantic_model unknown_model;
	pigen_semantic_model inout_model;
	pigen_resolve_error error = {0};
	const pigen_semantic_transport *left;
	const pigen_semantic_transport *right;
	const pigen_semantic_transport *queue;
	const pigen_semantic_module *first_module;
	const pigen_semantic_type *queue_type;
	const pigen_semantic_type *left_type;
	const pigen_semantic_type *boolean_type;
	const pigen_semantic_type *signed_type;
	const pigen_semantic_type *element_type;
	const pigen_semantic_type *selected_type;
	pigen_symbol_id left_typedef;
	const pigen_packed_dimension *queue_dimension;
	const pigen_packed_dimension *left_dimension;
	const pigen_packed_dimension *signed_dimension;
	const pigen_const_expr *bound;
	const pigen_const_expr *hex_constant;
	const pigen_const_expr *binary_constant;
	const pigen_const_expr *signed_constant;
	const pigen_const_expr *unknown_constant;
	const pigen_const_expr *wide_hex_constant;
	const pigen_const_expr *wide_decimal_constant;
	const pigen_bit_state *states;
	const pigen_semantic_parameter *width;
	const pigen_semantic_parameter *depth;
	const pigen_semantic_parameter *last;
	const pigen_semantic_parameter *enabled;
	const pigen_semantic_parameter *nonzero;
	const pigen_semantic_parameter *selected;
	const pigen_semantic_parameter *hex;
	const pigen_semantic_parameter *binary;
	const pigen_semantic_parameter *signed_value;
	const pigen_semantic_parameter *unknown_value;
	const pigen_semantic_parameter *mask_depth;
	const pigen_semantic_parameter *wide_hex;
	const pigen_semantic_parameter *wide_decimal;
	const pigen_semantic_expr *depth_value;
	const pigen_semantic_expr *enabled_value;
	const pigen_semantic_expr *nonzero_value;
	const pigen_semantic_expr *selected_value;
	const pigen_semantic_expr *hex_value;
	const pigen_semantic_expr *binary_value;
	const pigen_semantic_expr *mask_depth_value;
	const pigen_const_expr *depth_constant;
	const pigen_const_expr *enabled_constant;
	const pigen_const_expr *nonzero_constant;
	const pigen_const_expr *selected_constant;

	assert(pigen_preprocess(&sources, source, NULL, &preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&preprocessed.expanded, &syntax, &syntax_error));
	assert(pigen_resolve_declarations(&syntax, &model, &error));
	assert(model.compilation_scope.index != PIGEN_INVALID_ID);
	assert(model.module_count == 2);
	assert(model.parameter_count == 13);
	assert(model.transport_count == 4);
	first_module = pigen_module_get(&model, (pigen_module_id){0});
	assert(first_module && first_module->scope.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_module(&model, first_module->symbol).index == 0);
	assert(pigen_symbol_parameter(&model, first_module->symbol).index ==
		PIGEN_INVALID_ID);

	left = find_transport(&sources, &model, "left");
	right = find_transport(&sources, &model, "right");
	queue = find_transport(&sources, &model, "queue");
	assert(left && right && queue);
	width = find_parameter(&sources, &model, "WIDTH");
	depth = find_parameter(&sources, &model, "DEPTH");
	last = find_parameter(&sources, &model, "LAST");
	enabled = find_parameter(&sources, &model, "ENABLED");
	nonzero = find_parameter(&sources, &model, "NONZERO");
	selected = find_parameter(&sources, &model, "SELECTED");
	hex = find_parameter(&sources, &model, "HEX");
	binary = find_parameter(&sources, &model, "BINARY");
	signed_value = find_parameter(&sources, &model, "SIGNED_VALUE");
	unknown_value = find_parameter(&sources, &model, "UNKNOWN_VALUE");
	mask_depth = find_parameter(&sources, &model, "MASK_DEPTH");
	wide_hex = find_parameter(&sources, &model, "WIDE_HEX");
	wide_decimal = find_parameter(&sources, &model, "WIDE_DECIMAL");
	assert(width && depth && last && enabled && nonzero && selected && hex &&
		binary && signed_value && unknown_value && mask_depth && wide_hex &&
		wide_decimal);
	assert(pigen_parameter_get(&model,
		pigen_symbol_parameter(&model, width->symbol)) == width);
	assert(pigen_parameter_get(&model,
		pigen_symbol_parameter(&model, mask_depth->symbol)) == mask_depth);
	assert(pigen_symbol_transport(&model, width->symbol).index ==
		PIGEN_INVALID_ID);
	assert(last->is_local && nonzero->is_local);
	assert(!width->is_local && !depth->is_local && !enabled->is_local);
	assert(!selected->is_local);
	depth_value = pigen_expr_get(&model, depth->value);
	assert(depth_value && depth_value->kind == PIGEN_EXPR_BINARY);
	depth_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, depth->value));
	assert(depth_constant && depth_constant->kind == PIGEN_CONST_EXPR_BINARY);
	assert(pigen_const_expr_get(&model, depth_constant->as.binary.left)->kind ==
		PIGEN_CONST_EXPR_SYMBOL);
	enabled_value = pigen_expr_get(&model, enabled->value);
	enabled_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, enabled->value));
	assert(enabled_value && enabled_value->kind == PIGEN_EXPR_BINARY);
	assert(enabled_constant &&
		enabled_constant->kind == PIGEN_CONST_EXPR_BINARY &&
		enabled_constant->as.binary.operator == PIGEN_BINARY_LOGICAL_AND);
	nonzero_value = pigen_expr_get(&model, nonzero->value);
	nonzero_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, nonzero->value));
	assert(nonzero_value && nonzero_value->kind == PIGEN_EXPR_UNARY);
	assert(nonzero_constant &&
		nonzero_constant->kind == PIGEN_CONST_EXPR_UNARY &&
		nonzero_constant->as.unary.operator == PIGEN_UNARY_REDUCTION_OR);
	assert(enabled_value->type.index == nonzero_value->type.index);
	boolean_type = pigen_type_get(&model, enabled_value->type);
	assert(boolean_type && boolean_type->kind == PIGEN_TYPE_LOGIC &&
		boolean_type->signedness == PIGEN_SIGN_UNSIGNED &&
		boolean_type->dimension_count == 0);
	selected_value = pigen_expr_get(&model, selected->value);
	selected_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, selected->value));
	assert(selected_value && selected_value->kind == PIGEN_EXPR_CONDITIONAL);
	assert(selected_constant &&
		selected_constant->kind == PIGEN_CONST_EXPR_CONDITIONAL);
	assert(pigen_const_expr_get(&model,
		selected_constant->as.conditional.condition)->kind ==
		PIGEN_CONST_EXPR_SYMBOL);
	assert(pigen_const_expr_get(&model,
		selected_constant->as.conditional.when_true)->kind ==
		PIGEN_CONST_EXPR_SYMBOL);
	assert(selected_value->type.index == depth_value->type.index);
	hex_value = pigen_expr_get(&model, hex->value);
	binary_value = pigen_expr_get(&model, binary->value);
	hex_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, hex->value));
	binary_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, binary->value));
	assert(hex_value && binary_value && hex_value->kind == PIGEN_EXPR_BITS &&
		binary_value->kind == PIGEN_EXPR_BITS);
	assert(hex->value.index != binary->value.index);
	assert(hex_value->constant.index == binary_value->constant.index);
	assert(hex_value->type.index == binary_value->type.index);
	assert(hex_constant && binary_constant &&
		hex_constant->kind == PIGEN_CONST_EXPR_BITS &&
		binary_constant->kind == PIGEN_CONST_EXPR_BITS);
	states = pigen_const_expr_bits(&model, hex_value->constant);
	assert(states && hex_constant->as.bits.state_count == 8);
	for (size_t i = 0; i < 8; i++) assert(states[i] == PIGEN_BIT_ONE);
	signed_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, signed_value->value));
	assert(signed_constant && signed_constant->kind == PIGEN_CONST_EXPR_BITS);
	states = pigen_const_expr_bits(&model,
		pigen_expr_constant(&model, signed_value->value));
	assert(states && signed_constant->as.bits.state_count == 18);
	for (size_t i = 0; i < 18; i++)
		assert(states[i] == (i == 13 ? PIGEN_BIT_ONE : PIGEN_BIT_ZERO));
	signed_type = pigen_type_get(&model, signed_constant->type);
	assert(signed_type && signed_type->kind == PIGEN_TYPE_LOGIC &&
		signed_type->signedness == PIGEN_SIGN_SIGNED &&
		signed_type->dimension_count == 1);
	signed_dimension = pigen_type_dimensions(&model, signed_constant->type);
	assert(signed_dimension);
	bound = pigen_const_expr_get(&model, signed_dimension->left);
	assert(bound && bound->kind == PIGEN_CONST_EXPR_INTEGER &&
		bound->as.integer == 17);
	bound = pigen_const_expr_get(&model, signed_dimension->right);
	assert(bound && bound->kind == PIGEN_CONST_EXPR_INTEGER &&
		bound->as.integer == 0);
	unknown_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, unknown_value->value));
	assert(unknown_constant && unknown_constant->kind == PIGEN_CONST_EXPR_BITS);
	states = pigen_const_expr_bits(&model,
		pigen_expr_constant(&model, unknown_value->value));
	assert(states && unknown_constant->as.bits.state_count == 12);
	for (size_t i = 0; i < 4; i++) assert(states[i] == PIGEN_BIT_Z);
	assert(states[4] == PIGEN_BIT_ONE && states[5] == PIGEN_BIT_ONE);
	assert(states[6] == PIGEN_BIT_ZERO && states[7] == PIGEN_BIT_ZERO);
	for (size_t i = 8; i < 12; i++) assert(states[i] == PIGEN_BIT_X);
	wide_hex_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, wide_hex->value));
	wide_decimal_constant = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, wide_decimal->value));
	assert(wide_hex_constant && wide_decimal_constant &&
		wide_hex_constant->kind == PIGEN_CONST_EXPR_BITS &&
		wide_hex_constant == wide_decimal_constant &&
		wide_hex_constant->as.bits.state_count == 80);
	states = pigen_const_expr_bits(&model,
		pigen_expr_constant(&model, wide_hex->value));
	assert(states);
	for (size_t i = 0; i < 80; i++) assert(states[i] == PIGEN_BIT_ONE);
	mask_depth_value = pigen_expr_get(&model, mask_depth->value);
	assert(mask_depth_value &&
		mask_depth_value->kind == PIGEN_EXPR_CONDITIONAL &&
		mask_depth_value->type.index == hex_value->type.index);
	assert(left->payload_type.index == right->payload_type.index);
	assert(pigen_transport_get(&model,
		pigen_symbol_transport(&model, left->symbol)) == left);
	assert(pigen_transport_get(&model,
		pigen_symbol_transport(&model, queue->symbol)) == queue);
	assert(pigen_symbol_module(&model, left->symbol).index == PIGEN_INVALID_ID);
	assert(left->kind == PIGEN_SEMANTIC_BUF);
	assert(queue->kind == PIGEN_SEMANTIC_FIFO);
	assert(queue->fifo_depth.index != PIGEN_INVALID_ID);
	bound = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, queue->fifo_depth));
	assert(bound && bound->kind == PIGEN_CONST_EXPR_SYMBOL &&
		bound->as.symbol.index == mask_depth->symbol.index);
	left_type = pigen_type_get(&model, left->payload_type);
	assert(left_type && left_type->kind == PIGEN_TYPE_NAMED);
	left_typedef = left_type->named_symbol;
	element_type = pigen_type_get(&model,
		pigen_type_packed_element(&model, left->payload_type));
	assert(element_type && element_type->kind == PIGEN_TYPE_LOGIC &&
		element_type->signedness == PIGEN_SIGN_UNSIGNED &&
		element_type->dimension_count == 0);
	left_type = pigen_type_get(&model, left->payload_type);
	selected_type = pigen_type_get(&model,
		pigen_type_packed_select(&model, left->payload_type,
			pigen_type_dimensions(&model,
				pigen_symbol_get(&model, left_typedef)->type)->left,
			pigen_type_dimensions(&model,
				pigen_symbol_get(&model, left_typedef)->type)->right,
			PIGEN_SEMANTIC_SELECT_RANGE));
	assert(selected_type && selected_type->kind == PIGEN_TYPE_LOGIC &&
		selected_type->signedness == PIGEN_SIGN_UNSIGNED &&
		selected_type->dimension_count == 1);
	left_type = pigen_type_get(&model, left->payload_type);
	left_dimension = pigen_type_dimensions(&model,
		pigen_symbol_get(&model, left_type->named_symbol)->type);
	assert(left_dimension);
	bound = pigen_const_expr_get(&model, left_dimension->left);
	assert(bound && bound->kind == PIGEN_CONST_EXPR_SYMBOL &&
		bound->as.symbol.index == last->symbol.index);
	bound = pigen_const_expr_get(&model, left_dimension->right);
	assert(bound && bound->kind == PIGEN_CONST_EXPR_SYMBOL &&
		bound->as.symbol.index == nonzero->symbol.index);
	queue_type = pigen_type_get(&model, queue->payload_type);
	assert(queue_type && queue_type->kind == PIGEN_TYPE_NAMED);
	queue_type = pigen_type_get(&model,
		pigen_symbol_get(&model, queue_type->named_symbol)->type);
	assert(queue_type && queue_type->kind == PIGEN_TYPE_LOGIC);
	queue_dimension = pigen_type_dimensions(&model,
		pigen_symbol_get(&model,
			pigen_type_get(&model, queue->payload_type)->named_symbol)->type);
	assert(queue_dimension);
	bound = pigen_const_expr_get(&model, queue_dimension->left);
	assert(bound && bound->as.integer == 31);

	assert(pigen_preprocess(&sources, duplicate_source, NULL,
		&duplicate_preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&duplicate_preprocessed.expanded, &duplicate_syntax,
		&syntax_error));
	assert(!pigen_resolve_declarations(&duplicate_syntax,
		&duplicate_model, &error));
	assert(error.message && strstr(error.message, "duplicate module"));
	assert(span_is(&sources, error.span, "same"));

	assert(pigen_preprocess(&sources, unknown_source, NULL,
		&unknown_preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&unknown_preprocessed.expanded, &unknown_syntax,
		&syntax_error));
	assert(!pigen_resolve_declarations(&unknown_syntax,
		&unknown_model, &error));
	assert(error.message && strstr(error.message, "unknown type"));
	assert(span_is(&sources, error.span, "missing_t"));
	assert(pigen_preprocess(&sources, inout_source, NULL, &inout_preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&inout_preprocessed.expanded, &inout_syntax, &syntax_error));
	assert(!pigen_resolve_declarations(&inout_syntax, &inout_model, &error));
	assert(error.message && strstr(error.message, "not inout"));

	pigen_free_semantic_model(&inout_model);
	pigen_free_semantic_model(&unknown_model);
	pigen_free_semantic_model(&duplicate_model);
	pigen_free_semantic_model(&model);
	pigen_free_syntax_tree(&inout_syntax);
	pigen_free_syntax_tree(&unknown_syntax);
	pigen_free_syntax_tree(&duplicate_syntax);
	pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&inout_preprocessed);
	pigen_free_preprocess_result(&unknown_preprocessed);
	pigen_free_preprocess_result(&duplicate_preprocessed);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
	puts("PASS: parameters, typedefs, and transports resolve by scope and identity");
	return 0;
}
