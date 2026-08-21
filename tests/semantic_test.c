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
		"bit\n";
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
	pigen_expr_id shaped_concatenation_children[1];
	pigen_expr_id first_shape_index;
	pigen_expr_id second_shape_index;
	pigen_lvalue_id module_value_lvalue;
	pigen_data_type_id unsized_integer_data_type;
	pigen_data_type_id aliased_unsized_integer_type;
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
	pigen_symbol_id module_value;
	pigen_symbol_id pipeline_value;
	pigen_symbol_id first_local;
	pigen_signal_id module_value_signal;
	pigen_signal_id pipeline_value_signal;
	pigen_signal_id first_local_signal;
	pigen_symbol_id shadowed;
	pigen_symbol_id found;
	const pigen_transfer_type_laws *wire_laws;
	const pigen_transfer_type_laws *logic_laws;
	const pigen_transfer_type_laws *fifo_laws;
	size_t i;

	pigen_semantic_init(&model, &sources);
	scalar_shape = pigen_shape_intern(&model, NULL, 0);
	assert(scalar_shape.index != PIGEN_INVALID_ID);
	assert(scalar_shape.index ==
		pigen_semantic_scalar_shape(&model).index);
	assert(pigen_shape_get(&model, scalar_shape)->dimension_count == 0);
	wire_laws = pigen_transfer_type_get(PIGEN_TRANSFER_TYPE_WIRE);
	logic_laws = pigen_transfer_type_get(PIGEN_TRANSFER_TYPE_LOGIC);
	fifo_laws = pigen_transfer_type_get(PIGEN_TRANSFER_TYPE_FIFO);
	assert(wire_laws && wire_laws->is_static &&
		wire_laws->valid_constant == 1 && wire_laws->ready_constant == 0 &&
		!wire_laws->consumes_on_read && !wire_laws->produces_on_write &&
		!wire_laws->requires_ownership && !wire_laws->binds_domain);
	assert(logic_laws && logic_laws->is_static &&
		logic_laws->valid_constant == 1 && logic_laws->ready_constant == 1 &&
		!logic_laws->consumes_on_read && !logic_laws->produces_on_write &&
		!logic_laws->requires_ownership && !logic_laws->binds_domain);
	assert(fifo_laws && !fifo_laws->is_static &&
		fifo_laws->valid_constant < 0 && fifo_laws->ready_constant < 0 &&
		fifo_laws->consumes_on_read && fifo_laws->produces_on_write &&
		fifo_laws->requires_ownership && fifo_laws->binds_domain);
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
	left_bound = pigen_expr_add_integer(&model, 7,
		unsized_integer_data_type, range);
	right_bound = pigen_expr_add_integer(&model, 0,
		unsized_integer_data_type, range);
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
	conditional = pigen_expr_add_conditional(&model, right_bound, left_bound,
		right_bound, unsized_integer_data_type, range);
	same_conditional = pigen_expr_add_conditional(&model, right_bound,
		left_bound, right_bound, unsized_integer_data_type, range);
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
	assert(pigen_data_type_dimensions(&model, byte_type)->left.index == 0);
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
			upper->as.binary.operator == PIGEN_BINARY_SUBTRACT);
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
	bit_type = pigen_data_type_primitive_from_spelling(&model,
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
	assert(pigen_data_type_packed_width(&model, matrix_type).index !=
		PIGEN_INVALID_ID);
	assert(pigen_const_expr_get(&model,
		pigen_data_type_packed_width(&model, matrix_type))->kind ==
		PIGEN_CONST_EXPR_WIDTH_PRODUCT);

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
	assert(aliased_unsized_integer_type.index != PIGEN_INVALID_ID);
	assert(pigen_data_type_alias_target(&model,
		aliased_unsized_integer_type).index ==
		unsized_integer_data_type.index);
	assert(pigen_data_type_is_integral(&model, aliased_unsized_integer_type));
	assert(pigen_data_type_unary_result(&model, PIGEN_UNARY_NEGATE,
		aliased_unsized_integer_type).index ==
		aliased_unsized_integer_type.index);
	assert(pigen_data_type_unary_result(&model, PIGEN_UNARY_LOGICAL_NOT,
		aliased_unsized_integer_type).index == boolean_type.index);
	assert(pigen_data_type_binary_result(&model, PIGEN_BINARY_MULTIPLY,
		aliased_unsized_integer_type, aliased_unsized_integer_type).index ==
		aliased_unsized_integer_type.index);
	assert(pigen_data_type_binary_result(&model, PIGEN_BINARY_EQUAL,
		aliased_unsized_integer_type, unsized_integer_data_type).index ==
		boolean_type.index);
	assert(pigen_data_type_conditional_result(&model,
		aliased_unsized_integer_type, aliased_unsized_integer_type,
		aliased_unsized_integer_type).index ==
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
	assert(model.symbol_count == 5);
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
