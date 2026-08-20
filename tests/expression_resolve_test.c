#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/expression_resolve.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static pigen_syntax_expr_id parse(const pigen_preprocess_result *preprocessed,
	pigen_syntax_tree *syntax, size_t first, size_t after)
{
	pigen_syntax_error error = {0};
	pigen_syntax_expr_id result = INVALID_ID(pigen_syntax_expr_id);
	assert(pigen_parse_expression(&preprocessed->expanded, first, after,
		&syntax->expressions, &result, &error));
	return result;
}

int main(void)
{
	const char text[] =
		"width left\n"
		"left + width\n"
		"left == width\n"
		"width + width\n"
		"8'hA5[width]\n"
		"left[width]\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "expressions.pigen",
		text, strlen(text));
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_tree syntax = {0};
	pigen_semantic_model model;
	pigen_scope_id scope;
	pigen_type_id integer_type;
	pigen_type_id boolean_type;
	pigen_symbol_id width;
	pigen_symbol_id left;
	pigen_symbol_id shadowed;
	pigen_syntax_expr_id runtime_syntax;
	pigen_syntax_expr_id comparison_syntax;
	pigen_syntax_expr_id constant_syntax;
	pigen_syntax_expr_id constant_index_syntax;
	pigen_syntax_expr_id runtime_index_syntax;
	pigen_expr_id runtime;
	pigen_expr_id comparison;
	pigen_expr_id constant;
	pigen_expr_id constant_index;
	pigen_expr_id runtime_index;
	const pigen_semantic_expr *known;
	const pigen_semantic_expr *left_read;
	const pigen_semantic_expr *width_read;

	assert(pigen_preprocess(&sources, source, NULL, &preprocessed,
		&preprocess_error));
	syntax.expanded = &preprocessed.expanded;
	pigen_semantic_init(&model, &sources);
	scope = pigen_scope_add(&model, model.compilation_scope,
		(pigen_source_span){source, 0, strlen(text)});
	integer_type = pigen_semantic_integer_type(&model);
	boolean_type = pigen_semantic_boolean_result_type(&model);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_PARAMETER,
		integer_type, (pigen_source_span){source, 0, 5},
		(pigen_source_span){source, 0, 5}, &width, &shadowed) ==
		PIGEN_DECLARE_OK);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_TRANSPORT,
		integer_type, (pigen_source_span){source, 6, 10},
		(pigen_source_span){source, 6, 10}, &left, &shadowed) ==
		PIGEN_DECLARE_OK);

	/* Expanded token extents exclude the EOF token. */
	runtime_syntax = parse(&preprocessed, &syntax, 2, 5);
	comparison_syntax = parse(&preprocessed, &syntax, 5, 8);
	constant_syntax = parse(&preprocessed, &syntax, 8, 11);
	constant_index_syntax = parse(&preprocessed, &syntax, 11, 15);
	runtime_index_syntax = parse(&preprocessed, &syntax, 15, 19);

	runtime = pigen_resolve_expression(&syntax, &model, scope,
		runtime_syntax);
	known = pigen_expr_get(&model, runtime);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->type.index == integer_type.index);
	assert(known->as.binary.operator == PIGEN_BINARY_ADD);
	left_read = pigen_expr_get(&model, known->as.binary.left);
	width_read = pigen_expr_get(&model, known->as.binary.right);
	assert(left_read && left_read->kind == PIGEN_EXPR_SYMBOL);
	assert(left_read->as.symbol.index == left.index);
	assert(pigen_expr_constant(&model, known->as.binary.left).index ==
		PIGEN_INVALID_ID);
	assert(width_read && width_read->kind == PIGEN_EXPR_SYMBOL);
	assert(width_read->as.symbol.index == width.index);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, known->as.binary.right)) != NULL);
	assert(pigen_expr_constant(&model, runtime).index == PIGEN_INVALID_ID);
	assert(pigen_resolve_constant_expression(&syntax, &model, scope,
		runtime_syntax).index == PIGEN_INVALID_ID);

	comparison = pigen_resolve_expression(&syntax, &model, scope,
		comparison_syntax);
	known = pigen_expr_get(&model, comparison);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->type.index == boolean_type.index);
	assert(known->as.binary.operator == PIGEN_BINARY_EQUAL);
	assert(pigen_expr_constant(&model, comparison).index == PIGEN_INVALID_ID);

	constant = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_syntax);
	known = pigen_expr_get(&model, constant);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->type.index == integer_type.index);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant)) != NULL);

	constant_index = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_index_syntax);
	known = pigen_expr_get(&model, constant_index);
	assert(known && known->kind == PIGEN_EXPR_INDEX);
	assert(known->type.index == boolean_type.index);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_index))->kind ==
		PIGEN_CONST_EXPR_INDEX);

	runtime_index = pigen_resolve_expression(&syntax, &model, scope,
		runtime_index_syntax);
	known = pigen_expr_get(&model, runtime_index);
	assert(known && known->kind == PIGEN_EXPR_INDEX);
	assert(known->type.index == boolean_type.index);
	assert(pigen_expr_constant(&model, runtime_index).index ==
		PIGEN_INVALID_ID);

	pigen_free_semantic_model(&model);
	pigen_free_syntax_expr_arena(&syntax.expressions);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
	puts("PASS: runtime and constant expressions share typed resolution");
	return 0;
}
