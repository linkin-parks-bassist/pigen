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
	pigen_type_id integer_type;
	pigen_type_id byte_type;
	pigen_type_id same_byte_type;
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
	integer_type = pigen_type_intern(&model, PIGEN_TYPE_INTEGER, PIGEN_SIGN_SIGNED,
		INVALID_ID(pigen_symbol_id), NULL, 0, whole);
	dimension = (pigen_packed_dimension){
		pigen_expr_intern_integer(&model, 7, integer_type, range),
		pigen_expr_intern_integer(&model, 0, integer_type, range), range};
	byte_type = pigen_type_intern(&model, PIGEN_TYPE_LOGIC, PIGEN_SIGN_UNSIGNED,
		INVALID_ID(pigen_symbol_id), &dimension, 1, whole);
	same_byte_type = pigen_type_intern(&model, PIGEN_TYPE_LOGIC,
		PIGEN_SIGN_UNSIGNED, INVALID_ID(pigen_symbol_id), &dimension, 1, whole);
	assert(byte_type.index != PIGEN_INVALID_ID);
	assert(byte_type.index == same_byte_type.index);
	assert(pigen_type_get(&model, byte_type)->dimension_count == 1);
	assert(pigen_type_dimensions(&model, byte_type)->left.index == 0);

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
		pigen_packed_dimension distinct_dimension = {
			pigen_expr_intern_integer(&model, (uint64_t)i + 2, integer_type, range),
			pigen_expr_intern_integer(&model, (uint64_t)i + 66, integer_type, range),
			range};
		pigen_scope_id child = pigen_scope_add(&model, module_scope, whole);
		pigen_type_id distinct_type = pigen_type_intern(&model, PIGEN_TYPE_LOGIC,
			PIGEN_SIGN_UNSIGNED, INVALID_ID(pigen_symbol_id), &distinct_dimension,
			1, whole);
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
	assert(pigen_type_intern(&model, PIGEN_TYPE_LOGIC, PIGEN_SIGN_UNSIGNED,
		INVALID_ID(pigen_symbol_id), &dimension, 1,
		(pigen_source_span){source, 10, 2}).index == PIGEN_INVALID_ID);

	pigen_free_semantic_model(&model);
	pigen_free_sources(&sources);
	puts("PASS: scopes resolve stable symbols with structured types");
	return 0;
}
