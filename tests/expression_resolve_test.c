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
		"left[width]\n"
		"8'hA5[6:2]\n"
		"8'hA5[width +: 3]\n"
		"left[width -: 3]\n"
		"left[left:3]\n"
		"left[width +: left]\n"
		"{8'hA5, 8'h5A}\n"
		"{left, width}\n"
		"{width, left}\n"
		"word_t aliased\n"
		"aliased + aliased\n"
		"aliased == aliased\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "expressions.pigen",
		text, strlen(text));
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_tree syntax = {0};
	pigen_semantic_model model;
	pigen_scope_id scope;
	pigen_module_id module;
	pigen_type_id integer_type;
	pigen_type_id boolean_type;
	pigen_type_id aliased_type;
	pigen_symbol_id module_symbol;
	pigen_symbol_id width;
	pigen_symbol_id left;
	pigen_symbol_id word_type_symbol;
	pigen_symbol_id aliased;
	pigen_signal_id left_signal;
	pigen_signal_id aliased_signal;
	pigen_symbol_id shadowed;
	pigen_syntax_expr_id runtime_syntax;
	pigen_syntax_expr_id comparison_syntax;
	pigen_syntax_expr_id constant_syntax;
	pigen_syntax_expr_id constant_index_syntax;
	pigen_syntax_expr_id runtime_index_syntax;
	pigen_syntax_expr_id constant_range_syntax;
	pigen_syntax_expr_id constant_indexed_syntax;
	pigen_syntax_expr_id runtime_select_syntax;
	pigen_syntax_expr_id invalid_range_syntax;
	pigen_syntax_expr_id invalid_width_syntax;
	pigen_syntax_expr_id constant_concat_syntax;
	pigen_syntax_expr_id runtime_concat_syntax;
	pigen_syntax_expr_id swapped_concat_syntax;
	pigen_syntax_expr_id aliased_add_syntax;
	pigen_syntax_expr_id aliased_compare_syntax;
	pigen_expr_id runtime;
	pigen_expr_id comparison;
	pigen_expr_id constant;
	pigen_expr_id constant_index;
	pigen_expr_id runtime_index;
	pigen_expr_id constant_range;
	pigen_expr_id constant_indexed;
	pigen_expr_id runtime_select;
	pigen_expr_id constant_concat;
	pigen_expr_id runtime_concat;
	pigen_expr_id swapped_concat;
	pigen_expr_id aliased_add;
	pigen_expr_id aliased_compare;
	const pigen_semantic_expr *known;
	const pigen_semantic_expr *left_read;
	const pigen_semantic_expr *width_read;

	assert(pigen_preprocess(&sources, source, NULL, &preprocessed,
		&preprocess_error));
	syntax.expanded = &preprocessed.expanded;
	pigen_semantic_init(&model, &sources);
	model.compilation_scope = pigen_scope_add(&model,
		INVALID_ID(pigen_scope_id),
		(pigen_source_span){INVALID_ID(pigen_source_id), 0, 0});
	assert(pigen_symbol_declare(&model, model.compilation_scope,
		PIGEN_SYMBOL_MODULE, INVALID_ID(pigen_type_id),
		(pigen_source_span){source, 0, 5},
		(pigen_source_span){source, 0, strlen(text)}, &module_symbol, NULL) ==
		PIGEN_DECLARE_OK);
	scope = pigen_scope_add(&model, model.compilation_scope,
		(pigen_source_span){source, 0, strlen(text)});
	module = pigen_module_add(&model, (pigen_syntax_id){0}, module_symbol,
		scope, (pigen_source_span){source, 0, strlen(text)});
	assert(module.index != PIGEN_INVALID_ID);
	integer_type = pigen_semantic_integer_type(&model);
	boolean_type = pigen_semantic_boolean_result_type(&model);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_PARAMETER,
		integer_type, (pigen_source_span){source, 0, 5},
		(pigen_source_span){source, 0, 5}, &width, &shadowed) ==
		PIGEN_DECLARE_OK);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_SIGNAL,
		integer_type, (pigen_source_span){source, 6, 10},
		(pigen_source_span){source, 6, 10}, &left, &shadowed) ==
		PIGEN_DECLARE_OK);
	left_signal = pigen_signal_add(&model, (pigen_syntax_id){1}, module, left,
		integer_type, pigen_semantic_scalar_shape(&model),
		INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_LOGIC,
		PIGEN_SEMANTIC_INTERNAL, (pigen_source_span){source, 6, 10});
	assert(left_signal.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_TYPEDEF,
		integer_type,
		(pigen_source_span){source,
			(size_t)(strstr(text, "word_t") - text),
			(size_t)(strstr(text, "word_t") - text) + strlen("word_t")},
		(pigen_source_span){source,
			(size_t)(strstr(text, "word_t") - text),
			(size_t)(strstr(text, "word_t") - text) + strlen("word_t")},
		&word_type_symbol, &shadowed) == PIGEN_DECLARE_OK);
	aliased_type = pigen_type_intern(&model, PIGEN_TYPE_NAMED,
		PIGEN_SIGN_IMPLICIT, word_type_symbol, NULL, 0);
	assert(aliased_type.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_SIGNAL,
		aliased_type,
		(pigen_source_span){source,
			(size_t)(strstr(text, "aliased") - text),
			(size_t)(strstr(text, "aliased") - text) + strlen("aliased")},
		(pigen_source_span){source,
			(size_t)(strstr(text, "aliased") - text),
			(size_t)(strstr(text, "aliased") - text) + strlen("aliased")},
		&aliased, &shadowed) == PIGEN_DECLARE_OK);
	aliased_signal = pigen_signal_add(&model, (pigen_syntax_id){2}, module,
		aliased, aliased_type, pigen_semantic_scalar_shape(&model),
		INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_LOGIC,
		PIGEN_SEMANTIC_INTERNAL,
		(pigen_source_span){source,
			(size_t)(strstr(text, "aliased") - text),
			(size_t)(strstr(text, "aliased") - text) + strlen("aliased")});
	assert(aliased_signal.index != PIGEN_INVALID_ID);

	/* Expanded token extents exclude the EOF token. */
	runtime_syntax = parse(&preprocessed, &syntax, 2, 5);
	comparison_syntax = parse(&preprocessed, &syntax, 5, 8);
	constant_syntax = parse(&preprocessed, &syntax, 8, 11);
	constant_index_syntax = parse(&preprocessed, &syntax, 11, 15);
	runtime_index_syntax = parse(&preprocessed, &syntax, 15, 19);
	constant_range_syntax = parse(&preprocessed, &syntax, 19, 25);
	constant_indexed_syntax = parse(&preprocessed, &syntax, 25, 31);
	runtime_select_syntax = parse(&preprocessed, &syntax, 31, 37);
	invalid_range_syntax = parse(&preprocessed, &syntax, 37, 43);
	invalid_width_syntax = parse(&preprocessed, &syntax, 43, 49);
	constant_concat_syntax = parse(&preprocessed, &syntax, 49, 54);
	runtime_concat_syntax = parse(&preprocessed, &syntax, 54, 59);
	swapped_concat_syntax = parse(&preprocessed, &syntax, 59, 64);
	aliased_add_syntax = parse(&preprocessed, &syntax, 66, 69);
	aliased_compare_syntax = parse(&preprocessed, &syntax, 69, 72);

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

	constant_range = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_range_syntax);
	known = pigen_expr_get(&model, constant_range);
	assert(known && known->kind == PIGEN_EXPR_SELECT);
	assert(known->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_range))->kind ==
		PIGEN_CONST_EXPR_SELECT);

	constant_indexed = pigen_resolve_constant_expression(&syntax, &model,
		scope, constant_indexed_syntax);
	known = pigen_expr_get(&model, constant_indexed);
	assert(known && known->kind == PIGEN_EXPR_SELECT);
	assert(known->as.select.kind == PIGEN_SEMANTIC_SELECT_INDEXED_UP);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_indexed))->kind ==
		PIGEN_CONST_EXPR_SELECT);

	runtime_select = pigen_resolve_expression(&syntax, &model, scope,
		runtime_select_syntax);
	known = pigen_expr_get(&model, runtime_select);
	assert(known && known->kind == PIGEN_EXPR_SELECT);
	assert(known->as.select.kind == PIGEN_SEMANTIC_SELECT_INDEXED_DOWN);
	assert(pigen_expr_constant(&model, runtime_select).index ==
		PIGEN_INVALID_ID);
	assert(pigen_resolve_expression(&syntax, &model, scope,
		invalid_range_syntax).index == PIGEN_INVALID_ID);
	assert(pigen_resolve_expression(&syntax, &model, scope,
		invalid_width_syntax).index == PIGEN_INVALID_ID);

	constant_concat = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_concat_syntax);
	known = pigen_expr_get(&model, constant_concat);
	assert(known && known->kind == PIGEN_EXPR_CONCATENATION);
	assert(known->as.sequence.child_count == 2);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_concat))->kind ==
		PIGEN_CONST_EXPR_CONCATENATION);

	runtime_concat = pigen_resolve_expression(&syntax, &model, scope,
		runtime_concat_syntax);
	swapped_concat = pigen_resolve_expression(&syntax, &model, scope,
		swapped_concat_syntax);
	known = pigen_expr_get(&model, runtime_concat);
	assert(known && known->kind == PIGEN_EXPR_CONCATENATION);
	assert(known->type.index ==
		pigen_expr_get(&model, swapped_concat)->type.index);
	assert(pigen_expr_constant(&model, runtime_concat).index ==
		PIGEN_INVALID_ID);
	{
		const pigen_expr_id *children = pigen_expr_children(&model,
			known->as.sequence.first_child, known->as.sequence.child_count);
		assert(children && known->as.sequence.child_count == 2);
		assert(pigen_expr_get(&model, children[0])->as.symbol.index ==
			left.index);
		assert(pigen_expr_get(&model, children[1])->as.symbol.index ==
			width.index);
	}

	aliased_add = pigen_resolve_expression(&syntax, &model, scope,
		aliased_add_syntax);
	known = pigen_expr_get(&model, aliased_add);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->type.index == aliased_type.index);
	aliased_compare = pigen_resolve_expression(&syntax, &model, scope,
		aliased_compare_syntax);
	known = pigen_expr_get(&model, aliased_compare);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->type.index == boolean_type.index);

	pigen_free_semantic_model(&model);
	pigen_free_syntax_expr_arena(&syntax.expressions);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
	puts("PASS: runtime and constant expressions share typed resolution");
	return 0;
}
