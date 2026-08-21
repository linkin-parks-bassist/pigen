#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/predicate.h"
#include "pigen/resolve.h"

static int span_is(const pigen_source_manager *sources, pigen_source_span span,
	const char *expected)
{
	size_t length;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static const pigen_semantic_signal *find_signal(
	const pigen_source_manager *sources, const pigen_semantic_model *model,
	const char *name)
{
	size_t i;
	for (i = 0; i < model->signal_count; i++)
	{
		const pigen_symbol *symbol = pigen_symbol_get(model,
			model->signals[i].symbol);
		if (span_is(sources, symbol->name, name)) return &model->signals[i];
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

static int predicate_requires_symbol(const pigen_semantic_model *model,
	pigen_predicate_id predicate_id, pigen_symbol_id symbol, int expected)
{
	const pigen_predicate *predicate = pigen_predicate_get(model, predicate_id);
	const pigen_predicate_atom *atoms = pigen_predicate_atoms(model,
		predicate_id);
	size_t i;

	if (!predicate) return 0;
	for (i = 0; i < predicate->atom_count; i++)
	{
		const pigen_semantic_expr *condition = pigen_expr_get(model,
			atoms[i].condition);
		if (condition && condition->kind == PIGEN_EXPR_SYMBOL &&
			condition->as.symbol.index == symbol.index &&
			atoms[i].expected == expected) return 1;
	}
	return 0;
}

static int transfer_has_signal_use(const pigen_semantic_model *model,
	pigen_transfer_id transfer_id, pigen_signal_id signal, unsigned roles)
{
	const pigen_semantic_transfer *transfer = pigen_transfer_get(model,
		transfer_id);
	const pigen_transfer_signal_use *uses =
		pigen_transfer_signal_uses(model, transfer_id);
	size_t i;

	if (!transfer) return 0;
	for (i = 0; i < transfer->signal_use_count; i++)
		if (uses[i].signal.index == signal.index &&
			uses[i].roles == roles) return 1;
	return 0;
}

static void expect_resolve_error(const char *text, const char *expected)
{
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "error.pigen", text,
		strlen(text));
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_tree syntax = {0};
	pigen_syntax_error syntax_error = {0};
	pigen_semantic_model model;
	pigen_resolve_error error = {0};

	assert(pigen_preprocess(&sources, source, NULL, &preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&preprocessed.expanded, &syntax, &syntax_error));
	assert(!pigen_resolve_semantics(&syntax, &model, &error));
	assert(error.message && strstr(error.message, expected));
	pigen_free_semantic_model(&model);
	pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
}

int main(void)
{
	const char text[] =
		"typedef logic [31:0] word_t;\n"
		"module first #(parameter WIDTH = 8, DEPTH = WIDTH / 2, ENABLED = DEPTH >= 4 && !(WIDTH == 0), SELECTED = ENABLED ? DEPTH : WIDTH, HEX = 8'hff, BINARY = 8'b1111_1111, SIGNED_VALUE = 18'sd8192, UNKNOWN_VALUE = 12'hx3z, MASK_DEPTH = HEX == BINARY ? 8'd4 : 8'd2, WIDE_HEX = 80'hffff_ffff_ffff_ffff_ffff, WIDE_DECIMAL = 80'd1208925819614629174706175) (input logic clk, reset, select, input logic [7:0] samples[0:3], masks[4], output reg [7:0] count);\n"
		"  localparam LAST = WIDTH - 1;\n"
		"  localparam NONZERO = |WIDTH;\n"
		"  typedef logic unsigned [LAST:NONZERO] byte_t;\n"
		"  wire ready;\n"
		"  logic [WIDTH-1:0] state, next_state;\n"
		"  logic [7:0] memory [0:3][4];\n"
		"  buf byte_t left, right, alternate;\n"
		"  buf bit gate;\n"
		"  fifo word_t[MASK_DEPTH] queue;\n"
		"  always_ff @(posedge clk) begin\n"
		"    if (select) begin\n"
		"      if (gate) begin right <= left; end\n"
		"      else begin alternate <= left; end\n"
		"    end else begin right <= left; end\n"
		"  end\n"
		"  always @(posedge clk) state <= next_state;\n"
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
	const pigen_semantic_signal *left;
	const pigen_semantic_signal *right;
	const pigen_semantic_signal *alternate;
	const pigen_semantic_signal *gate;
	const pigen_semantic_signal *queue;
	const pigen_semantic_signal *clk;
	const pigen_semantic_signal *reset;
	const pigen_semantic_signal *select;
	const pigen_semantic_signal *count;
	const pigen_semantic_signal *ready;
	const pigen_semantic_signal *state;
	const pigen_semantic_signal *next_state;
	const pigen_semantic_signal *memory;
	const pigen_semantic_signal *samples;
	const pigen_semantic_signal *masks;
	const pigen_semantic_module *first_module;
	const pigen_semantic_clock_domain *clock_domain;
	const pigen_semantic_process *first_process;
	const pigen_semantic_process *second_process;
	const pigen_semantic_transfer *first_transfer;
	const pigen_semantic_transfer *second_transfer;
	const pigen_semantic_transfer *third_transfer;
	const pigen_semantic_transfer *fourth_transfer;
	const pigen_semantic_lvalue *transfer_destination;
	const pigen_semantic_expr *transfer_value;
	const pigen_transfer_signal_use *transfer_uses;
	pigen_data_type_id queue_type;
	pigen_data_type_id left_type;
	pigen_data_type_id boolean_type;
	pigen_data_type_id signed_type;
	pigen_data_type_id element_type;
	pigen_data_type_id selected_type;
	pigen_symbol_id left_typedef;
	const pigen_packed_dimension *queue_dimension;
	const pigen_packed_dimension *left_dimension;
	const pigen_packed_dimension *signed_dimension;
	const pigen_shape_dimension *memory_dimensions;
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
	assert(pigen_resolve_semantics(&syntax, &model, &error));
	assert(model.compilation_scope.index != PIGEN_INVALID_ID);
	assert(model.module_count == 2);
	assert(model.parameter_count == 13);
	assert(model.signal_count == 16);
	assert(model.clock_domain_count == 1);
	assert(model.process_count == 2);
	assert(model.transfer_count == 4);
	first_module = pigen_module_get(&model, (pigen_module_id){0});
	assert(first_module && first_module->scope.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_module(&model, first_module->symbol).index == 0);
	assert(pigen_symbol_parameter(&model, first_module->symbol).index ==
		PIGEN_INVALID_ID);

	left = find_signal(&sources, &model, "left");
	right = find_signal(&sources, &model, "right");
	alternate = find_signal(&sources, &model, "alternate");
	gate = find_signal(&sources, &model, "gate");
	queue = find_signal(&sources, &model, "queue");
	assert(left && right && alternate && gate && queue);
	clk = find_signal(&sources, &model, "clk");
	reset = find_signal(&sources, &model, "reset");
	select = find_signal(&sources, &model, "select");
	count = find_signal(&sources, &model, "count");
	ready = find_signal(&sources, &model, "ready");
	state = find_signal(&sources, &model, "state");
	next_state = find_signal(&sources, &model, "next_state");
	memory = find_signal(&sources, &model, "memory");
	samples = find_signal(&sources, &model, "samples");
	masks = find_signal(&sources, &model, "masks");
	assert(clk && reset && select && count && ready && state && next_state &&
		memory && samples && masks);
	assert(pigen_shape_dimensions(&model, samples->shape)[0].form ==
		PIGEN_SHAPE_DIMENSION_RANGE);
	assert(pigen_shape_dimensions(&model, masks->shape)[0].form ==
		PIGEN_SHAPE_DIMENSION_COUNT);
	memory_dimensions = pigen_shape_dimensions(&model, memory->shape);
	assert(memory_dimensions &&
		pigen_shape_get(&model, memory->shape)->dimension_count == 2);
	assert(memory_dimensions[0].form == PIGEN_SHAPE_DIMENSION_RANGE);
	bound = pigen_const_expr_get(&model,
		memory_dimensions[0].as.range.left);
	assert(bound && bound->kind == PIGEN_CONST_EXPR_INTEGER &&
		bound->as.integer == 0);
	bound = pigen_const_expr_get(&model,
		memory_dimensions[0].as.range.right);
	assert(bound && bound->kind == PIGEN_CONST_EXPR_INTEGER &&
		bound->as.integer == 3);
	assert(memory_dimensions[1].form == PIGEN_SHAPE_DIMENSION_COUNT);
	bound = pigen_const_expr_get(&model, memory_dimensions[1].as.count);
	assert(bound && bound->kind == PIGEN_CONST_EXPR_INTEGER &&
		bound->as.integer == 4);
	assert(clk->direction == PIGEN_SEMANTIC_INPUT &&
		clk->transfer_type == PIGEN_TRANSFER_TYPE_WIRE);
	assert(reset->data_type.index == clk->data_type.index);
	assert(count->direction == PIGEN_SEMANTIC_OUTPUT &&
		count->transfer_type == PIGEN_TRANSFER_TYPE_REG);
	assert(ready->direction == PIGEN_SEMANTIC_INTERNAL &&
		ready->transfer_type == PIGEN_TRANSFER_TYPE_WIRE);
	assert(state->transfer_type == PIGEN_TRANSFER_TYPE_LOGIC &&
		state->data_type.index == next_state->data_type.index);
	assert(pigen_signal_get(&model,
		pigen_symbol_signal(&model, clk->symbol)) == clk);
	assert(pigen_signal_get(&model,
		pigen_symbol_signal(&model, state->symbol)) == state);
	clock_domain = pigen_clock_domain_get(&model, (pigen_clock_domain_id){0});
	first_process = pigen_process_get(&model, (pigen_process_id){0});
	second_process = pigen_process_get(&model, (pigen_process_id){1});
	first_transfer = pigen_transfer_get(&model, (pigen_transfer_id){0});
	second_transfer = pigen_transfer_get(&model, (pigen_transfer_id){1});
	third_transfer = pigen_transfer_get(&model, (pigen_transfer_id){2});
	fourth_transfer = pigen_transfer_get(&model, (pigen_transfer_id){3});
	assert(clock_domain && first_process && second_process && first_transfer &&
		second_transfer && third_transfer && fourth_transfer);
	assert(clock_domain->clock_symbol.index == clk->symbol.index &&
		clock_domain->edge == PIGEN_SEMANTIC_POSEDGE);
	assert(first_process->domain.index == second_process->domain.index &&
		first_process->domain.index == 0);
	assert(first_transfer->process.index == 0 &&
		second_transfer->process.index == 0 &&
		third_transfer->process.index == 0 &&
		fourth_transfer->process.index == 1);
	assert(first_transfer->domain.index == second_transfer->domain.index &&
		first_transfer->domain.index == third_transfer->domain.index &&
		first_transfer->domain.index == fourth_transfer->domain.index);
	assert(pigen_predicates_mutually_exclusive(&model, first_transfer->guard,
		second_transfer->guard));
	assert(pigen_predicates_mutually_exclusive(&model, first_transfer->guard,
		third_transfer->guard));
	assert(pigen_predicates_mutually_exclusive(&model, second_transfer->guard,
		third_transfer->guard));
	assert(pigen_predicate_get(&model, first_transfer->guard)->atom_count == 2);
	assert(pigen_predicate_get(&model, second_transfer->guard)->atom_count == 2);
	assert(pigen_predicate_get(&model, third_transfer->guard)->atom_count == 1);
	assert(predicate_requires_symbol(&model, first_transfer->guard,
		select->symbol, 1));
	assert(predicate_requires_symbol(&model, first_transfer->guard,
		gate->symbol, 1));
	assert(predicate_requires_symbol(&model, second_transfer->guard,
		select->symbol, 1));
	assert(predicate_requires_symbol(&model, second_transfer->guard,
		gate->symbol, 0));
	assert(predicate_requires_symbol(&model, third_transfer->guard,
		select->symbol, 0));
	assert(first_transfer->guard.index != model.true_predicate.index &&
		second_transfer->guard.index != model.true_predicate.index &&
		third_transfer->guard.index != model.true_predicate.index &&
		fourth_transfer->guard.index == model.true_predicate.index);
	assert(first_transfer->signal_use_count == 4 &&
		second_transfer->signal_use_count == 4 &&
		third_transfer->signal_use_count == 3 &&
		fourth_transfer->signal_use_count == 2);
	transfer_uses = pigen_transfer_signal_uses(&model,
		(pigen_transfer_id){0});
	assert(transfer_uses);
	assert(transfer_has_signal_use(&model, (pigen_transfer_id){0},
		pigen_symbol_signal(&model, right->symbol),
		PIGEN_TRANSFER_SIGNAL_WRITE | PIGEN_TRANSFER_PRODUCER));
	assert(transfer_has_signal_use(&model, (pigen_transfer_id){0},
		pigen_symbol_signal(&model, left->symbol),
		PIGEN_TRANSFER_SIGNAL_READ | PIGEN_TRANSFER_CONSUMER));
	assert(transfer_has_signal_use(&model, (pigen_transfer_id){0},
		pigen_symbol_signal(&model, gate->symbol),
		PIGEN_TRANSFER_SIGNAL_READ | PIGEN_TRANSFER_CONSUMER));
	assert(transfer_has_signal_use(&model, (pigen_transfer_id){0},
		pigen_symbol_signal(&model, select->symbol), PIGEN_TRANSFER_SIGNAL_READ));
	assert(transfer_has_signal_use(&model, (pigen_transfer_id){3},
		pigen_symbol_signal(&model, state->symbol), PIGEN_TRANSFER_SIGNAL_WRITE));
	assert(transfer_has_signal_use(&model, (pigen_transfer_id){3},
		pigen_symbol_signal(&model, next_state->symbol), PIGEN_TRANSFER_SIGNAL_READ));
	transfer_destination = pigen_lvalue_get(&model,
		first_transfer->destination);
	transfer_value = pigen_expr_get(&model, first_transfer->value);
	assert(transfer_destination &&
		transfer_destination->kind == PIGEN_LVALUE_PROJECTION);
	assert(transfer_destination->as.projection.base_symbol.index == right->symbol.index);
	assert(transfer_destination->as.projection.signal.index ==
		pigen_symbol_signal(&model, right->symbol).index);
	assert(transfer_value && transfer_value->kind == PIGEN_EXPR_SYMBOL &&
		transfer_value->as.symbol.index == left->symbol.index);
	transfer_destination = pigen_lvalue_get(&model,
		second_transfer->destination);
	transfer_value = pigen_expr_get(&model, second_transfer->value);
	assert(transfer_destination &&
		transfer_destination->as.projection.base_symbol.index ==
			alternate->symbol.index &&
		transfer_destination->as.projection.signal.index ==
			pigen_symbol_signal(&model, alternate->symbol).index);
	assert(transfer_value && transfer_value->kind == PIGEN_EXPR_SYMBOL &&
		transfer_value->as.symbol.index == left->symbol.index);
	transfer_destination = pigen_lvalue_get(&model,
		third_transfer->destination);
	transfer_value = pigen_expr_get(&model, third_transfer->value);
	assert(transfer_destination &&
		transfer_destination->as.projection.base_symbol.index == right->symbol.index &&
		transfer_destination->as.projection.signal.index ==
			pigen_symbol_signal(&model, right->symbol).index);
	assert(transfer_value && transfer_value->kind == PIGEN_EXPR_SYMBOL &&
		transfer_value->as.symbol.index == left->symbol.index);
	transfer_destination = pigen_lvalue_get(&model,
		fourth_transfer->destination);
	transfer_value = pigen_expr_get(&model, fourth_transfer->value);
	assert(transfer_destination &&
		transfer_destination->as.projection.base_symbol.index == state->symbol.index &&
		transfer_destination->as.projection.signal.index ==
			pigen_symbol_signal(&model, state->symbol).index);
	assert(transfer_value && transfer_value->kind == PIGEN_EXPR_SYMBOL &&
		transfer_value->as.symbol.index == next_state->symbol.index);
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
	assert(pigen_symbol_signal(&model, width->symbol).index ==
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
	assert(enabled_value->data_type.index == nonzero_value->data_type.index);
	boolean_type = enabled_value->data_type;
	assert(pigen_data_type_exists(&model, boolean_type) &&
		pigen_data_type_signedness(&model, boolean_type) ==
			PIGEN_SIGN_UNSIGNED &&
		pigen_data_type_dimension_count(&model, boolean_type) == 0);
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
	assert(selected_value->data_type.index == depth_value->data_type.index);
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
	assert(hex_value->data_type.index == binary_value->data_type.index);
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
	signed_type = signed_constant->data_type;
	assert(pigen_data_type_exists(&model, signed_type) &&
		pigen_data_type_signedness(&model, signed_type) == PIGEN_SIGN_SIGNED &&
		pigen_data_type_dimension_count(&model, signed_type) == 1);
	signed_dimension = pigen_data_type_dimensions(&model, signed_constant->data_type);
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
		mask_depth_value->data_type.index == hex_value->data_type.index);
	assert(left->data_type.index == right->data_type.index);
	assert(pigen_signal_get(&model,
		pigen_symbol_signal(&model, left->symbol)) == left);
	assert(pigen_signal_get(&model,
		pigen_symbol_signal(&model, queue->symbol)) == queue);
	assert(pigen_symbol_module(&model, left->symbol).index == PIGEN_INVALID_ID);
	assert(left->transfer_type == PIGEN_TRANSFER_TYPE_BUF);
	assert(left->domain.index == 0 && right->domain.index == 0 &&
		alternate->domain.index == 0 && gate->domain.index == 0);
	assert(queue->transfer_type == PIGEN_TRANSFER_TYPE_FIFO);
	assert(queue->domain.index == PIGEN_INVALID_ID);
	assert(queue->fifo_depth.index != PIGEN_INVALID_ID);
	bound = pigen_const_expr_get(&model,
		pigen_expr_constant(&model, queue->fifo_depth));
	assert(bound && bound->kind == PIGEN_CONST_EXPR_SYMBOL &&
		bound->as.symbol.index == mask_depth->symbol.index);
	left_type = left->data_type;
	left_typedef = pigen_data_type_alias_symbol(&model, left_type);
	assert(left_typedef.index != PIGEN_INVALID_ID);
	assert(pigen_data_type_packed_width(&model, left->data_type).index ==
		pigen_data_type_packed_width(&model,
			pigen_symbol_get(&model, left_typedef)->data_type).index);
	element_type = pigen_data_type_packed_element(&model, left->data_type);
	assert(pigen_data_type_exists(&model, element_type) &&
		pigen_data_type_signedness(&model, element_type) ==
			PIGEN_SIGN_UNSIGNED &&
		pigen_data_type_dimension_count(&model, element_type) == 0);
	left_type = left->data_type;
	selected_type = pigen_data_type_packed_select(&model, left->data_type,
			pigen_data_type_dimensions(&model,
				pigen_symbol_get(&model, left_typedef)->data_type)->left,
			pigen_data_type_dimensions(&model,
				pigen_symbol_get(&model, left_typedef)->data_type)->right,
			PIGEN_SEMANTIC_SELECT_RANGE);
	assert(pigen_data_type_exists(&model, selected_type) &&
		pigen_data_type_signedness(&model, selected_type) ==
			PIGEN_SIGN_UNSIGNED &&
		pigen_data_type_dimension_count(&model, selected_type) == 1);
	left_type = left->data_type;
	left_dimension = pigen_data_type_dimensions(&model,
		pigen_symbol_get(&model,
			pigen_data_type_alias_symbol(&model, left_type))->data_type);
	assert(left_dimension);
	bound = pigen_const_expr_get(&model, left_dimension->left);
	assert(bound && bound->kind == PIGEN_CONST_EXPR_SYMBOL &&
		bound->as.symbol.index == last->symbol.index);
	bound = pigen_const_expr_get(&model, left_dimension->right);
	assert(bound && bound->kind == PIGEN_CONST_EXPR_SYMBOL &&
		bound->as.symbol.index == nonzero->symbol.index);
	queue_type = queue->data_type;
	assert(pigen_data_type_alias_symbol(&model, queue_type).index !=
		PIGEN_INVALID_ID);
	queue_type = pigen_symbol_get(&model,
		pigen_data_type_alias_symbol(&model, queue_type))->data_type;
	assert(pigen_data_type_exists(&model, queue_type));
	queue_dimension = pigen_data_type_dimensions(&model,
		pigen_symbol_get(&model,
			pigen_data_type_alias_symbol(&model, queue->data_type))->data_type);
	assert(queue_dimension);
	bound = pigen_const_expr_get(&model, queue_dimension->left);
	assert(bound && bound->as.integer == 31);

	assert(pigen_preprocess(&sources, duplicate_source, NULL,
		&duplicate_preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&duplicate_preprocessed.expanded, &duplicate_syntax,
		&syntax_error));
	assert(!pigen_resolve_semantics(&duplicate_syntax,
		&duplicate_model, &error));
	assert(error.message && strstr(error.message, "duplicate module"));
	assert(span_is(&sources, error.span, "same"));

	assert(pigen_preprocess(&sources, unknown_source, NULL,
		&unknown_preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&unknown_preprocessed.expanded, &unknown_syntax,
		&syntax_error));
	assert(!pigen_resolve_semantics(&unknown_syntax,
		&unknown_model, &error));
	assert(error.message && strstr(error.message, "unknown type"));
	assert(span_is(&sources, error.span, "missing_t"));
	assert(pigen_preprocess(&sources, inout_source, NULL, &inout_preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&inout_preprocessed.expanded, &inout_syntax, &syntax_error));
	assert(!pigen_resolve_semantics(&inout_syntax, &inout_model, &error));
	assert(error.message && strstr(error.message, "not inout"));

	expect_resolve_error(
		"module bad(input logic clk); wire x; logic y; "
		"always @(posedge clk) x <= y; endmodule\n",
		"not a writable");
	expect_resolve_error(
		"module bad(input logic clk, input buf [7:0] x); "
		"always @(posedge clk) x <= 8'h0; endmodule\n",
		"not a writable");
	expect_resolve_error(
		"module bad(input logic clk); buf [7:0] x; "
		"always @(posedge clk) x <= x; endmodule\n",
		"cannot source its destination");
	expect_resolve_error(
		"module bad(input logic clk_a, clk_b); buf [7:0] x, y; "
		"always @(posedge clk_a) y <= x; "
		"always @(posedge clk_b) y <= x; endmodule\n",
		"across clock domains");
	expect_resolve_error(
		"module bad(input logic clk); buf [7:0] x, y, z; "
		"always @(posedge clk) begin y <= x; z <= x; end endmodule\n",
		"nonexclusive consumers");
	expect_resolve_error(
		"module bad(input logic clk); buf [7:0] x, y, z; "
		"always @(posedge clk) begin y <= x; y <= z; end endmodule\n",
		"nonexclusive producers");
	expect_resolve_error(
		"module bad(input logic clk); buf bit x[2]; buf bit y[3]; "
		"always @(posedge clk) y <= x; endmodule\n",
		"shape mismatch");

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
	puts("PASS: guarded transfers, domains, and ownership resolve by identity");
	return 0;
}
