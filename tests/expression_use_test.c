#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/expression_resolve.h"
#include "pigen/expression_use.h"
#include "pigen/resolve.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static int token_is(const pigen_expanded_source *source, size_t at,
	const char *expected)
{
	const pigen_expanded_token *token = pigen_expanded_token_get(source,
		(pigen_token_id){(uint32_t)at});
	const char *text;
	size_t length;
	if (!token) return 0;
	text = pigen_expanded_token_text(source, token, &length);
	return text && length == strlen(expected) &&
		!memcmp(text, expected, length);
}

int main(void)
{
	const char text[] =
		"module uses #(parameter TOP = 6, WIDTH = 3);\n"
		"  buf [7:0] left, right;\n"
		"endmodule\n"
		"left ? right : left\n"
		"(right)\n"
		"right[left]\n"
		"(right[left])\n"
		"right[left][left]\n"
		"right[TOP:2]\n"
		"(right[left +: WIDTH])\n"
		"right[6 -: left]\n"
		"right[left:2]\n"
		"{right[left], left[TOP:2], right}\n"
		"{right, TOP}\n"
		"{{right, left}, right}\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "uses.pigen", text,
		strlen(text));
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_error syntax_error = {0};
	pigen_resolve_error resolve_error = {0};
	pigen_syntax_tree syntax = {0};
	pigen_semantic_model model;
	pigen_syntax_expr_id syntax_expression = INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_lvalue = INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_index = INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_index_lvalue =
		INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_invalid_index =
		INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_range = INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_select_lvalue =
		INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_invalid_width =
		INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_invalid_range =
		INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_concat = INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_invalid_concat =
		INVALID_ID(pigen_syntax_expr_id);
	pigen_syntax_expr_id syntax_nested_concat =
		INVALID_ID(pigen_syntax_expr_id);
	pigen_expr_id expression;
	pigen_expr_id lvalue_expression;
	pigen_expr_id index_expression;
	pigen_expr_id index_lvalue_expression;
	pigen_expr_id range_expression;
	pigen_expr_id select_lvalue_expression;
	pigen_expr_id concat_expression;
	pigen_expr_id invalid_concat_expression;
	pigen_expr_id nested_concat_expression;
	pigen_lvalue_id lvalue;
	pigen_lvalue_id index_lvalue;
	pigen_lvalue_id select_lvalue;
	pigen_lvalue_id concat_lvalue;
	pigen_lvalue_id nested_concat_lvalue;
	pigen_predicate_id always;
	pigen_expression_use_analysis analysis = {0};
	const pigen_semantic_expr *conditional;
	const pigen_semantic_expr *indexed;
	const pigen_semantic_expr *indexed_base;
	const pigen_semantic_expr *indexed_subscript;
	const pigen_semantic_expr *range;
	const pigen_semantic_expr *select_lvalue_root;
	const pigen_semantic_expr *select_lvalue_projection;
	const pigen_packed_dimension *select_dimensions;
	const pigen_const_expr *select_upper;
	const pigen_const_expr *select_width;
	const pigen_semantic_expr *concat;
	const pigen_expr_id *concat_children;
	const pigen_semantic_lvalue *concat_lvalue_node;
	const pigen_lvalue_id *concat_lvalue_children;
	const pigen_predicate_atom *true_atoms;
	const pigen_predicate_atom *false_atoms;
	size_t first;

	assert(pigen_preprocess(&sources, source, NULL, &preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&preprocessed.expanded, &syntax, &syntax_error));
	assert(pigen_resolve_semantics(&syntax, &model, &resolve_error));
	for (first = 0; first < preprocessed.expanded.token_count; first++)
		if (token_is(&preprocessed.expanded, first, "endmodule")) break;
	assert(first + 1 < preprocessed.expanded.token_count);
	first++;
	assert(preprocessed.expanded.token_count - 1 == first + 80);
	assert(pigen_parse_expression(&preprocessed.expanded, first, first + 5,
		&syntax.expressions,
		&syntax_expression, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 5, first + 8,
		&syntax.expressions, &syntax_lvalue, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 8, first + 12,
		&syntax.expressions, &syntax_index, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 12, first + 18,
		&syntax.expressions, &syntax_index_lvalue, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 18, first + 25,
		&syntax.expressions, &syntax_invalid_index, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 25, first + 31,
		&syntax.expressions, &syntax_range, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 31, first + 39,
		&syntax.expressions, &syntax_select_lvalue, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 39, first + 45,
		&syntax.expressions, &syntax_invalid_width, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 45, first + 51,
		&syntax.expressions, &syntax_invalid_range, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 51, first + 66,
		&syntax.expressions, &syntax_concat, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 66, first + 71,
		&syntax.expressions, &syntax_invalid_concat, &syntax_error));
	assert(pigen_parse_expression(&preprocessed.expanded, first + 71, first + 80,
		&syntax.expressions, &syntax_nested_concat, &syntax_error));
	expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope,
		syntax_expression);
	lvalue_expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope, syntax_lvalue);
	index_expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope, syntax_index);
	index_lvalue_expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope,
		syntax_index_lvalue);
	range_expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope, syntax_range);
	select_lvalue_expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope,
		syntax_select_lvalue);
	concat_expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope, syntax_concat);
	invalid_concat_expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope,
		syntax_invalid_concat);
	nested_concat_expression = pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope,
		syntax_nested_concat);
	assert(pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope,
		syntax_invalid_index).index == PIGEN_INVALID_ID);
	assert(pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope,
		syntax_invalid_width).index == PIGEN_INVALID_ID);
	assert(pigen_resolve_expression(&syntax, &model,
		pigen_module_get(&model, (pigen_module_id){0})->scope,
		syntax_invalid_range).index == PIGEN_INVALID_ID);
	conditional = pigen_expr_get(&model, expression);
	indexed = pigen_expr_get(&model, index_expression);
	assert(conditional && conditional->kind == PIGEN_EXPR_CONDITIONAL);
	assert(indexed && indexed->kind == PIGEN_EXPR_INDEX);
	indexed_base = pigen_expr_get(&model, indexed->as.index.base);
	indexed_subscript = pigen_expr_get(&model, indexed->as.index.index);
	assert(indexed_base && indexed_base->kind == PIGEN_EXPR_SYMBOL);
	assert(indexed_subscript &&
		indexed_subscript->kind == PIGEN_EXPR_SYMBOL);
	always = pigen_predicate_true(&model);
	assert(pigen_analyze_expression_uses(&model, expression, always,
		PIGEN_EXPRESSION_USE_READ, &analysis));

	assert(analysis.use_count == 3);
	assert(analysis.signal_count == 2);
	assert(analysis.uses[0].symbol.index ==
		pigen_expr_get(&model, conditional->as.conditional.condition)->
		as.symbol.index);
	assert(analysis.uses[0].predicate.index == always.index);
	assert(analysis.uses[0].signal.index ==
		analysis.uses[2].signal.index);
	assert(analysis.uses[0].expression.index !=
		analysis.uses[2].expression.index);
	assert(analysis.uses[1].signal.index !=
		analysis.uses[0].signal.index);
	assert(analysis.uses[0].context == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.signals[0].contexts == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.signals[1].contexts == PIGEN_EXPRESSION_USE_READ);

	true_atoms = pigen_predicate_atoms(&model, analysis.uses[1].predicate);
	false_atoms = pigen_predicate_atoms(&model, analysis.uses[2].predicate);
	assert(pigen_predicate_get(&model,
		analysis.uses[1].predicate)->atom_count == 1);
	assert(pigen_predicate_get(&model,
		analysis.uses[2].predicate)->atom_count == 1);
	assert(true_atoms && false_atoms);
	assert(true_atoms[0].condition.index ==
		conditional->as.conditional.condition.index);
	assert(false_atoms[0].condition.index ==
		conditional->as.conditional.condition.index);
	assert(true_atoms[0].expected && !false_atoms[0].expected);
	assert(pigen_predicates_mutually_exclusive(&model,
		analysis.uses[1].predicate, analysis.uses[2].predicate));

	lvalue = pigen_lvalue_resolve(&model, lvalue_expression);
	assert(lvalue.index != PIGEN_INVALID_ID);
	assert(pigen_lvalue_resolve(&model, lvalue_expression).index == lvalue.index);
	assert(pigen_lvalue_resolve(&model, expression).index == PIGEN_INVALID_ID);
	assert(pigen_lvalue_get(&model, lvalue)->expression.index ==
		lvalue_expression.index);
	assert(pigen_lvalue_get(&model, lvalue)->kind ==
		PIGEN_LVALUE_PROJECTION);
	assert(pigen_lvalue_get(&model,
		lvalue)->as.projection.signal.index ==
		analysis.uses[1].signal.index);
	assert(pigen_analyze_lvalue_uses(&model, lvalue, always, &analysis));
	assert(analysis.use_count == 4);
	assert(analysis.signal_count == 2);
	assert(analysis.uses[3].expression.index == lvalue_expression.index);
	assert(analysis.uses[3].context == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.uses[3].signal.index == analysis.uses[1].signal.index);
	assert(analysis.signals[1].contexts ==
		(PIGEN_EXPRESSION_USE_READ | PIGEN_EXPRESSION_USE_LVALUE));

	pigen_free_expression_use_analysis(&analysis);
	assert(indexed->data_type.index ==
		pigen_data_type_boolean(&model).index);
	assert(pigen_analyze_expression_uses(&model, index_expression, always,
		PIGEN_EXPRESSION_USE_READ, &analysis));
	assert(analysis.use_count == 2);
	assert(analysis.signal_count == 2);
	assert(analysis.uses[0].expression.index == index_expression.index);
	assert(analysis.uses[0].symbol.index == indexed_base->as.symbol.index);
	assert(analysis.uses[0].context == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.uses[1].symbol.index ==
		indexed_subscript->as.symbol.index);
	assert(analysis.uses[1].context == PIGEN_EXPRESSION_USE_INDEX);
	assert(analysis.uses[0].signal.index !=
		analysis.uses[1].signal.index);
	assert(analysis.signals[0].contexts == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.signals[1].contexts == PIGEN_EXPRESSION_USE_INDEX);

	pigen_free_expression_use_analysis(&analysis);
	index_lvalue = pigen_lvalue_resolve(&model, index_lvalue_expression);
	assert(index_lvalue.index != PIGEN_INVALID_ID);
	assert(pigen_lvalue_get(&model, index_lvalue)->expression.index ==
		index_lvalue_expression.index);
	assert(pigen_lvalue_get(&model, index_lvalue)->data_type.index ==
		pigen_data_type_boolean(&model).index);
	assert(pigen_lvalue_get(&model, index_lvalue)->kind ==
		PIGEN_LVALUE_PROJECTION);
	assert(pigen_lvalue_get(&model,
		index_lvalue)->as.projection.base_symbol.index ==
		indexed_base->as.symbol.index);
	assert(pigen_analyze_lvalue_uses(&model, index_lvalue, always, &analysis));
	assert(analysis.use_count == 2);
	assert(analysis.signal_count == 2);
	assert(analysis.uses[0].expression.index ==
		index_lvalue_expression.index);
	assert(analysis.uses[0].context == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.uses[1].context == PIGEN_EXPRESSION_USE_INDEX);
	assert(analysis.signals[0].contexts == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.signals[1].contexts == PIGEN_EXPRESSION_USE_INDEX);

	pigen_free_expression_use_analysis(&analysis);
	range = pigen_expr_get(&model, range_expression);
	assert(range && range->kind == PIGEN_EXPR_SELECT);
	assert(range->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE);
	assert(pigen_analyze_expression_uses(&model, range_expression, always,
		PIGEN_EXPRESSION_USE_READ, &analysis));
	assert(analysis.use_count == 2);
	assert(analysis.signal_count == 1);
	assert(analysis.uses[0].expression.index == range_expression.index);
	assert(analysis.uses[0].context == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.uses[1].context == PIGEN_EXPRESSION_USE_TYPE);

	pigen_free_expression_use_analysis(&analysis);
	select_lvalue_root = pigen_expr_get(&model, select_lvalue_expression);
	assert(select_lvalue_root &&
		select_lvalue_root->kind == PIGEN_EXPR_GROUP);
	select_lvalue_projection = pigen_expr_get(&model,
		select_lvalue_root->as.group.operand);
	assert(select_lvalue_projection &&
		select_lvalue_projection->kind == PIGEN_EXPR_SELECT);
	assert(select_lvalue_projection->as.select.kind ==
		PIGEN_SEMANTIC_SELECT_INDEXED_UP);
	select_dimensions = pigen_data_type_dimensions(&model,
		select_lvalue_root->data_type);
	assert(select_dimensions &&
		pigen_data_type_dimension_count(&model,
			select_lvalue_root->data_type) == 1);
	select_upper = pigen_const_expr_get(&model, select_dimensions->left);
	assert(select_upper && select_upper->kind == PIGEN_CONST_EXPR_BINARY &&
		select_upper->as.binary.operation.operator ==
			PIGEN_BINARY_SUBTRACT);
	select_width = pigen_const_expr_get(&model,
		select_upper->as.binary.left);
	assert(select_width &&
		select_width->kind == PIGEN_CONST_EXPR_SELECT_WIDTH &&
		select_width->as.select_width.kind ==
			PIGEN_SEMANTIC_SELECT_INDEXED_UP &&
		select_width->as.select_width.left.index == PIGEN_INVALID_ID);
	select_lvalue = pigen_lvalue_resolve(&model, select_lvalue_expression);
	assert(select_lvalue.index != PIGEN_INVALID_ID);
	assert(pigen_analyze_lvalue_uses(&model, select_lvalue, always, &analysis));
	assert(analysis.use_count == 3);
	assert(analysis.signal_count == 2);
	assert(analysis.uses[0].expression.index ==
		select_lvalue_expression.index);
	assert(analysis.uses[0].context == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.uses[1].context == PIGEN_EXPRESSION_USE_INDEX);
	assert(analysis.uses[2].context == PIGEN_EXPRESSION_USE_TYPE);
	assert(analysis.signals[0].contexts == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.signals[1].contexts == PIGEN_EXPRESSION_USE_INDEX);

	pigen_free_expression_use_analysis(&analysis);
	concat = pigen_expr_get(&model, concat_expression);
	assert(concat && concat->kind == PIGEN_EXPR_CONCATENATION);
	concat_children = pigen_expr_children(&model,
		concat->as.sequence.first_child, concat->as.sequence.child_count);
	assert(concat_children && concat->as.sequence.child_count == 3);
	assert(pigen_analyze_expression_uses(&model, concat_expression, always,
		PIGEN_EXPRESSION_USE_READ, &analysis));
	assert(analysis.use_count == 5);
	assert(analysis.signal_count == 2);
	assert(analysis.uses[0].expression.index == concat_children[0].index);
	assert(analysis.uses[0].context == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.uses[1].context == PIGEN_EXPRESSION_USE_INDEX);
	assert(analysis.uses[2].expression.index == concat_children[1].index);
	assert(analysis.uses[2].context == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.uses[3].context == PIGEN_EXPRESSION_USE_TYPE);
	assert(analysis.uses[4].expression.index == concat_children[2].index);
	assert(analysis.uses[4].context == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.signals[0].contexts == PIGEN_EXPRESSION_USE_READ);
	assert(analysis.signals[1].contexts ==
		(PIGEN_EXPRESSION_USE_READ | PIGEN_EXPRESSION_USE_INDEX));

	pigen_free_expression_use_analysis(&analysis);
	concat_lvalue = pigen_lvalue_resolve(&model, concat_expression);
	concat_lvalue_node = pigen_lvalue_get(&model, concat_lvalue);
	assert(concat_lvalue_node &&
		concat_lvalue_node->kind == PIGEN_LVALUE_CONCATENATION);
	concat_lvalue_children = pigen_lvalue_children(&model,
		concat_lvalue_node->as.sequence.first_child,
		concat_lvalue_node->as.sequence.child_count);
	assert(concat_lvalue_children &&
		concat_lvalue_node->as.sequence.child_count == 3);
	for (size_t i = 0; i < 3; i++)
	{
		const pigen_semantic_lvalue *child =
			pigen_lvalue_get(&model, concat_lvalue_children[i]);
		assert(child && child->kind == PIGEN_LVALUE_PROJECTION);
		assert(child->expression.index == concat_children[i].index);
	}
	assert(pigen_analyze_lvalue_uses(&model, concat_lvalue, always, &analysis));
	assert(analysis.use_count == 5);
	assert(analysis.signal_count == 2);
	assert(analysis.uses[0].expression.index == concat_children[0].index);
	assert(analysis.uses[0].context == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.uses[1].context == PIGEN_EXPRESSION_USE_INDEX);
	assert(analysis.uses[2].expression.index == concat_children[1].index);
	assert(analysis.uses[2].context == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.uses[3].context == PIGEN_EXPRESSION_USE_TYPE);
	assert(analysis.uses[4].expression.index == concat_children[2].index);
	assert(analysis.uses[4].context == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.signals[0].contexts == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.signals[1].contexts ==
		(PIGEN_EXPRESSION_USE_LVALUE | PIGEN_EXPRESSION_USE_INDEX));

	pigen_free_expression_use_analysis(&analysis);
	assert(pigen_lvalue_resolve(&model, invalid_concat_expression).index ==
		PIGEN_INVALID_ID);
	nested_concat_lvalue = pigen_lvalue_resolve(&model,
		nested_concat_expression);
	concat_lvalue_node = pigen_lvalue_get(&model, nested_concat_lvalue);
	assert(concat_lvalue_node &&
		concat_lvalue_node->kind == PIGEN_LVALUE_CONCATENATION);
	concat_lvalue_children = pigen_lvalue_children(&model,
		concat_lvalue_node->as.sequence.first_child,
		concat_lvalue_node->as.sequence.child_count);
	assert(concat_lvalue_children &&
		concat_lvalue_node->as.sequence.child_count == 2);
	assert(pigen_lvalue_get(&model, concat_lvalue_children[0])->kind ==
		PIGEN_LVALUE_CONCATENATION);
	assert(pigen_lvalue_get(&model, concat_lvalue_children[1])->kind ==
		PIGEN_LVALUE_PROJECTION);
	assert(pigen_analyze_lvalue_uses(&model, nested_concat_lvalue, always,
		&analysis));
	assert(analysis.use_count == 3);
	assert(analysis.signal_count == 2);
	assert(analysis.signals[0].contexts == PIGEN_EXPRESSION_USE_LVALUE);
	assert(analysis.signals[1].contexts == PIGEN_EXPRESSION_USE_LVALUE);

	pigen_free_expression_use_analysis(&analysis);
	pigen_free_semantic_model(&model);
	pigen_free_syntax_tree(&syntax);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
	puts("PASS: expression uses retain identity, projection, and predicate");
	return 0;
}
