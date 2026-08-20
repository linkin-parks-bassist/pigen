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
		"endmodule\n";
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
	pigen_expr_id left_bound;
	pigen_expr_id right_bound;
	pigen_expr_id conditional;
	pigen_expr_id same_conditional;
	pigen_expr_id module_value_expression;
	pigen_lvalue_id module_value_lvalue;
	pigen_type_id integer_type;
	pigen_type_id boolean_type;
	pigen_type_id byte_type;
	pigen_type_id same_byte_type;
	pigen_type_id matrix_type;
	pigen_type_id row_type;
	pigen_type_id element_type;
	pigen_scope_id module_scope;
	pigen_scope_id pipeline_scope;
	pigen_scope_id first_stage;
	pigen_scope_id second_stage;
	pigen_symbol_id module_value;
	pigen_symbol_id pipeline_value;
	pigen_symbol_id first_local;
	pigen_symbol_id shadowed;
	pigen_symbol_id found;
	size_t i;

	pigen_semantic_init(&model, &sources);
	integer_type = pigen_semantic_integer_type(&model);
	boolean_type = pigen_semantic_boolean_result_type(&model);
	assert(integer_type.index == pigen_semantic_integer_type(&model).index);
	assert(boolean_type.index ==
		pigen_semantic_boolean_result_type(&model).index);
	assert(pigen_type_get(&model, integer_type)->kind == PIGEN_TYPE_INTEGER);
	assert(pigen_type_get(&model, boolean_type)->kind == PIGEN_TYPE_LOGIC);
	left_bound = pigen_expr_add_integer(&model, 7, integer_type, range);
	right_bound = pigen_expr_add_integer(&model, 0, integer_type, range);
	conditional = pigen_expr_add_conditional(&model, right_bound, left_bound,
		right_bound, integer_type, range);
	same_conditional = pigen_expr_add_conditional(&model, right_bound,
		left_bound, right_bound, integer_type, range);
	assert(conditional.index != same_conditional.index);
	assert(pigen_expr_constant(&model, conditional).index ==
		pigen_expr_constant(&model, same_conditional).index);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, conditional))->kind ==
		PIGEN_CONST_EXPR_CONDITIONAL);
	dimension = (pigen_packed_dimension){
		pigen_expr_constant(&model, left_bound),
		pigen_expr_constant(&model, right_bound)};
	byte_type = pigen_type_intern(&model, PIGEN_TYPE_LOGIC, PIGEN_SIGN_UNSIGNED,
		INVALID_ID(pigen_symbol_id), &dimension, 1);
	same_byte_type = pigen_type_intern(&model, PIGEN_TYPE_LOGIC,
		PIGEN_SIGN_UNSIGNED, INVALID_ID(pigen_symbol_id), &dimension, 1);
	assert(byte_type.index != PIGEN_INVALID_ID);
	assert(byte_type.index == same_byte_type.index);
	assert(pigen_type_get(&model, byte_type)->dimension_count == 1);
	assert(pigen_type_dimensions(&model, byte_type)->left.index == 0);
	matrix_dimensions[0] = dimension;
	matrix_dimensions[1] = dimension;
	matrix_type = pigen_type_intern(&model, PIGEN_TYPE_LOGIC,
		PIGEN_SIGN_SIGNED, INVALID_ID(pigen_symbol_id), matrix_dimensions, 2);
	row_type = pigen_type_packed_element(&model, matrix_type);
	element_type = pigen_type_packed_element(&model, row_type);
	assert(pigen_type_get(&model, row_type)->kind == PIGEN_TYPE_LOGIC);
	assert(pigen_type_get(&model, row_type)->signedness ==
		PIGEN_SIGN_UNSIGNED);
	assert(pigen_type_get(&model, row_type)->dimension_count == 1);
	assert(pigen_type_get(&model, element_type)->kind == PIGEN_TYPE_LOGIC);
	assert(pigen_type_get(&model, element_type)->signedness ==
		PIGEN_SIGN_UNSIGNED);
	assert(pigen_type_get(&model, element_type)->dimension_count == 0);
	assert(pigen_type_packed_element(&model, element_type).index ==
		PIGEN_INVALID_ID);
	assert(pigen_type_packed_element(&model, integer_type).index ==
		boolean_type.index);

	module_scope = pigen_scope_add(&model, INVALID_ID(pigen_scope_id), whole);
	pipeline_scope = pigen_scope_add(&model, module_scope, whole);
	first_stage = pigen_scope_add(&model, pipeline_scope, whole);
	second_stage = pigen_scope_add(&model, pipeline_scope, whole);
	assert(module_scope.index != PIGEN_INVALID_ID);
	assert(pigen_scope_add(&model, (pigen_scope_id){9999}, whole).index ==
		PIGEN_INVALID_ID);

	assert(pigen_symbol_declare(&model, module_scope, PIGEN_SYMBOL_VALUE,
		byte_type, first_value, whole, &module_value, &shadowed) == PIGEN_DECLARE_OK);
	assert(shadowed.index == PIGEN_INVALID_ID);
	module_value_expression = pigen_expr_add_symbol(&model, module_value,
		byte_type, first_value);
	module_value_lvalue = pigen_lvalue_resolve(&model, module_value_expression);
	assert(module_value_lvalue.index != PIGEN_INVALID_ID);
	assert(pigen_lvalue_get(&model, module_value_lvalue)->base_symbol.index ==
		module_value.index);
	assert(pigen_lvalue_get(&model, module_value_lvalue)->transport.index ==
		PIGEN_INVALID_ID);
	assert(pigen_symbol_declare(&model, pipeline_scope, PIGEN_SYMBOL_VALUE,
		byte_type, second_value, whole, &pipeline_value, &shadowed) == PIGEN_DECLARE_OK);
	assert(shadowed.index == module_value.index);
	assert(pigen_symbol_declare(&model, first_stage, PIGEN_SYMBOL_VALUE,
		byte_type, third_value, whole, &first_local, &shadowed) == PIGEN_DECLARE_OK);
	assert(shadowed.index == pipeline_value.index);

	found = pigen_symbol_lookup(&model, first_stage, first_value);
	assert(found.index == first_local.index);
	found = pigen_symbol_lookup(&model, second_stage, first_value);
	assert(found.index == pipeline_value.index);
	assert(pigen_symbol_declare(&model, pipeline_scope, PIGEN_SYMBOL_VALUE,
		byte_type, third_value, whole, &found, NULL) == PIGEN_DECLARE_DUPLICATE);
	assert(found.index == pipeline_value.index);
	assert(model.symbol_count == 3);

	for (i = 0; i < 64; i++)
	{
		pigen_expr_id distinct_left = pigen_expr_add_integer(&model,
			(uint64_t)i + 2, integer_type, range);
		pigen_expr_id distinct_right = pigen_expr_add_integer(&model,
			(uint64_t)i + 66, integer_type, range);
		pigen_packed_dimension distinct_dimension = {
			pigen_expr_constant(&model, distinct_left),
			pigen_expr_constant(&model, distinct_right)};
		pigen_scope_id child = pigen_scope_add(&model, module_scope, whole);
		pigen_type_id distinct_type = pigen_type_intern(&model, PIGEN_TYPE_LOGIC,
			PIGEN_SIGN_UNSIGNED, INVALID_ID(pigen_symbol_id), &distinct_dimension,
			1);
		assert(child.index != PIGEN_INVALID_ID);
		assert(distinct_type.index != PIGEN_INVALID_ID);
		assert(pigen_symbol_declare(&model, child, PIGEN_SYMBOL_VALUE,
			distinct_type, first_value, whole, NULL, NULL) == PIGEN_DECLARE_OK);
	}
	assert(pigen_type_get(&model, byte_type)->dimension_count == 1);
	assert(pigen_symbol_get(&model, module_value)->scope.index == module_scope.index);
	assert(pigen_symbol_lookup(&model, first_stage, second_value).index ==
		first_local.index);

	assert(pigen_symbol_declare(&model, module_scope, PIGEN_SYMBOL_VALUE,
		(pigen_type_id){9999}, first_value, whole, NULL, NULL) ==
		PIGEN_DECLARE_INVALID);
	pigen_free_semantic_model(&model);
	pigen_free_sources(&sources);
	puts("PASS: scopes resolve stable symbols with structured types");
	return 0;
}
