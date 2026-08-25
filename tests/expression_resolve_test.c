#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/expression_analysis.h"
#include "pigen/expression_resolve.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})

static pigen_syntax_expr_id parse(const pigen_preprocess_result *preprocessed,
	pigen_syntax_tree *syntax, size_t first, size_t after)
{
	pigen_syntax_error error = {0};
	pigen_syntax_expr_id result = INVALID_ID(pigen_syntax_expr_id);
	assert(pigen_parse_expression(&preprocessed->expanded, first, after,
		&syntax->expressions, &syntax->types, &result, &error));
	return result;
}

static pigen_symbol_id declare_signal(pigen_semantic_model *model,
	pigen_module_id module, pigen_scope_id scope, pigen_data_type_id type,
	pigen_source_span name, pigen_syntax_id syntax)
{
	pigen_symbol_id symbol;
	pigen_symbol_id shadowed;

	assert(pigen_symbol_declare(model, scope, PIGEN_SYMBOL_SIGNAL, type, name,
		name, &symbol, &shadowed) == PIGEN_DECLARE_OK);
	assert(pigen_signal_add(model, syntax, module, symbol, type,
		pigen_semantic_scalar_shape(model), INVALID_ID(pigen_expr_id),
		PIGEN_TRANSFER_TYPE_LOGIC, PIGEN_SEMANTIC_INTERNAL, name).index !=
		PIGEN_INVALID_ID);
	return symbol;
}

static uint64_t evaluate_width(const pigen_semantic_model *model,
	pigen_const_expr_id width)
{
	const pigen_const_expr *known = pigen_const_expr_get(model, width);
	const pigen_const_expr_id *children;
	uint64_t value;
	size_t i;

	assert(known);
	if (known->kind == PIGEN_CONST_EXPR_INTEGER) return known->as.integer;
	assert(known->kind == PIGEN_CONST_EXPR_WIDTH_SUM ||
		known->kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT ||
		known->kind == PIGEN_CONST_EXPR_WIDTH_MAXIMUM);
	children = pigen_const_expr_children(model, known->as.sequence.first_child,
		known->as.sequence.child_count);
	value = known->kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT ? 1 : 0;
	for (i = 0; i < known->as.sequence.child_count; i++)
	{
		uint64_t child = evaluate_width(model, children[i]);
		if (known->kind == PIGEN_CONST_EXPR_WIDTH_SUM) value += child;
		else if (known->kind == PIGEN_CONST_EXPR_WIDTH_PRODUCT) value *= child;
		else if (child > value) value = child;
	}
	return value;
}

static uint64_t resolved_width(pigen_semantic_model *model,
	pigen_data_type_id type)
{
	return evaluate_width(model, pigen_data_type_packed_width(model, type));
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
		"aliased == aliased\n"
		"(sa + sb) * sc\n"
		"ua + 1\n"
		"sa + ua\n"
		"uint[8]'(sa)\n"
		"byte'(ua)\n"
		"8'hff + ua\n"
		"uint[8]'(ua)\n"
		"uint[8]'(bp)\n"
		"byte'(8'hff)\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "expressions.pigen",
		text, strlen(text));
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_tree syntax = {0};
	pigen_semantic_model model;
	pigen_scope_id scope;
	pigen_module_id module;
	pigen_data_type_id unsized_integer_data_type;
	pigen_data_type_id boolean_type;
	pigen_data_type_id aliased_type;
	pigen_binary_resolution binary_resolution;
	pigen_symbol_id module_symbol;
	pigen_symbol_id width;
	pigen_symbol_id left;
	pigen_symbol_id word_type_symbol;
	pigen_symbol_id aliased;
	pigen_symbol_id signed_a;
	pigen_symbol_id signed_b;
	pigen_symbol_id signed_c;
	pigen_symbol_id unsigned_a;
	pigen_symbol_id byte_parameter;
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
	pigen_syntax_expr_id chained_syntax;
	pigen_syntax_expr_id literal_add_syntax;
	pigen_syntax_expr_id mixed_add_syntax;
	pigen_syntax_expr_id integer_cast_syntax;
	pigen_syntax_expr_id byte_cast_syntax;
	pigen_syntax_expr_id invalid_mixed_syntax;
	pigen_syntax_expr_id identity_cast_syntax;
	pigen_syntax_expr_id constant_cast_syntax;
	pigen_syntax_expr_id invalid_cast_syntax;
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
	pigen_expr_id intrinsic;
	pigen_expr_id literal_add;
	pigen_expr_id mixed_add;
	pigen_expr_id integer_cast;
	pigen_expr_id byte_cast;
	pigen_expr_id identity_cast;
	pigen_expr_id runtime_constant_cast;
	pigen_expr_id constant_cast;
	pigen_data_type_id signed_8;
	pigen_data_type_id unsigned_8;
	pigen_const_expr_id width_8;
	pigen_semantic_error semantic_error = {0};
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
		PIGEN_SYMBOL_MODULE, INVALID_ID(pigen_data_type_id),
		(pigen_source_span){source, 0, 5},
		(pigen_source_span){source, 0, strlen(text)}, &module_symbol, NULL) ==
		PIGEN_DECLARE_OK);
	scope = pigen_scope_add(&model, model.compilation_scope,
		(pigen_source_span){source, 0, strlen(text)});
	module = pigen_module_add(&model, (pigen_syntax_id){0}, module_symbol,
		scope, (pigen_source_span){source, 0, strlen(text)});
	assert(module.index != PIGEN_INVALID_ID);
	unsized_integer_data_type = pigen_data_type_unsized_integer(&model);
	boolean_type = pigen_data_type_boolean(&model);
	assert(pigen_data_type_resolve_binary_operation(&model, PIGEN_BINARY_ADD,
		unsized_integer_data_type, unsized_integer_data_type, &binary_resolution));
	assert(binary_resolution.left_conversion.kind == PIGEN_CONVERSION_IDENTITY);
	assert(binary_resolution.left_conversion.source_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.left_conversion.target_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.right_conversion.kind == PIGEN_CONVERSION_IDENTITY);
	assert(binary_resolution.right_conversion.source_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.right_conversion.target_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.operation.operator == PIGEN_BINARY_ADD);
	assert(binary_resolution.operation.left_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.operation.right_data_type.index ==
		unsized_integer_data_type.index);
	assert(binary_resolution.operation.result_data_type.index ==
		unsized_integer_data_type.index);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_PARAMETER,
		unsized_integer_data_type, (pigen_source_span){source, 0, 5},
		(pigen_source_span){source, 0, 5}, &width, &shadowed) ==
		PIGEN_DECLARE_OK);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_SIGNAL,
		unsized_integer_data_type, (pigen_source_span){source, 6, 10},
		(pigen_source_span){source, 6, 10}, &left, &shadowed) ==
		PIGEN_DECLARE_OK);
	left_signal = pigen_signal_add(&model, (pigen_syntax_id){1}, module, left,
		unsized_integer_data_type, pigen_semantic_scalar_shape(&model),
		INVALID_ID(pigen_expr_id), PIGEN_TRANSFER_TYPE_LOGIC,
		PIGEN_SEMANTIC_INTERNAL, (pigen_source_span){source, 6, 10});
	assert(left_signal.index != PIGEN_INVALID_ID);
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_TYPEDEF,
		unsized_integer_data_type,
		(pigen_source_span){source,
			(size_t)(strstr(text, "word_t") - text),
			(size_t)(strstr(text, "word_t") - text) + strlen("word_t")},
		(pigen_source_span){source,
			(size_t)(strstr(text, "word_t") - text),
			(size_t)(strstr(text, "word_t") - text) + strlen("word_t")},
		&word_type_symbol, &shadowed) == PIGEN_DECLARE_OK);
	aliased_type = pigen_data_type_alias(&model, word_type_symbol,
		unsized_integer_data_type, PIGEN_SIGN_IMPLICIT, NULL, 0);
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
	width_8 = pigen_const_expr_intern_integer(&model, 8,
		unsized_integer_data_type);
	signed_8 = pigen_data_type_signed_integer(&model, width_8);
	unsigned_8 = pigen_data_type_unsigned_integer(&model, width_8);
	signed_a = declare_signal(&model, module, scope, signed_8,
		(pigen_source_span){source, (size_t)(strstr(text, "sa + sb") - text),
			(size_t)(strstr(text, "sa + sb") - text) + 2},
		(pigen_syntax_id){3});
	signed_b = declare_signal(&model, module, scope, signed_8,
		(pigen_source_span){source, (size_t)(strstr(text, "sb) *") - text),
			(size_t)(strstr(text, "sb) *") - text) + 2},
		(pigen_syntax_id){4});
	signed_c = declare_signal(&model, module, scope, signed_8,
		(pigen_source_span){source, (size_t)(strstr(text, "sc\n") - text),
			(size_t)(strstr(text, "sc\n") - text) + 2},
		(pigen_syntax_id){5});
	unsigned_a = declare_signal(&model, module, scope, unsigned_8,
		(pigen_source_span){source, (size_t)(strstr(text, "ua + 1") - text),
			(size_t)(strstr(text, "ua + 1") - text) + 2},
		(pigen_syntax_id){6});
	assert(pigen_symbol_declare(&model, scope, PIGEN_SYMBOL_PARAMETER,
		pigen_data_type_byte(&model),
		(pigen_source_span){source,
			(size_t)(strstr(text, "bp)") - text),
			(size_t)(strstr(text, "bp)") - text) + 2},
		(pigen_source_span){source,
			(size_t)(strstr(text, "bp)") - text),
			(size_t)(strstr(text, "bp)") - text) + 2},
		&byte_parameter, &shadowed) == PIGEN_DECLARE_OK);
	(void)signed_a;
	(void)signed_b;
	(void)signed_c;
	(void)unsigned_a;
	(void)byte_parameter;

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
	chained_syntax = parse(&preprocessed, &syntax, 72, 79);
	literal_add_syntax = parse(&preprocessed, &syntax, 79, 82);
	mixed_add_syntax = parse(&preprocessed, &syntax, 82, 85);
	integer_cast_syntax = parse(&preprocessed, &syntax, 85, 93);
	byte_cast_syntax = parse(&preprocessed, &syntax, 93, 98);
	invalid_mixed_syntax = parse(&preprocessed, &syntax, 98, 101);
	identity_cast_syntax = parse(&preprocessed, &syntax, 101, 109);
	constant_cast_syntax = parse(&preprocessed, &syntax, 109, 117);
	invalid_cast_syntax = parse(&preprocessed, &syntax, 117, 122);
	{
		pigen_analyzed_expr_arena analyzed_arena = {0};
		pigen_analyzed_expr_id analyzed;
		size_t expression_count = model.expression_count;

		assert(pigen_analyze_expression(&syntax, &model, scope,
			integer_cast_syntax, 0, &analyzed_arena, &analyzed,
			&semantic_error));
		assert(analyzed.index != PIGEN_INVALID_ID);
		assert(model.expression_count == expression_count);
		pigen_free_analyzed_expr_arena(&analyzed_arena);
	}

	intrinsic = pigen_resolve_expression(&syntax, &model, scope,
		chained_syntax, &semantic_error);
	known = pigen_expr_get(&model, intrinsic);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(resolved_width(&model, known->data_type) == 17);
	{
		const pigen_semantic_expr *group = pigen_expr_get(&model,
			known->as.binary.left);
		const pigen_semantic_expr *addition;

		assert(group && group->kind == PIGEN_EXPR_GROUP);
		addition = pigen_expr_get(&model, group->as.group.operand);
		assert(addition && addition->kind == PIGEN_EXPR_BINARY);
		assert(resolved_width(&model, addition->data_type) == 9);
	}

	literal_add = pigen_resolve_expression(&syntax, &model, scope,
		literal_add_syntax, &semantic_error);
	known = pigen_expr_get(&model, literal_add);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(resolved_width(&model, known->data_type) == 9);
	{
		const pigen_semantic_expr *left_operand = pigen_expr_get(&model,
			known->as.binary.left);
		const pigen_semantic_expr *right_operand = pigen_expr_get(&model,
			known->as.binary.right);
		const pigen_semantic_expr *exact;

		assert(left_operand && left_operand->kind == PIGEN_EXPR_SYMBOL);
		assert(right_operand && right_operand->kind == PIGEN_EXPR_CONVERSION);
		assert(right_operand->as.conversion.conversion.kind ==
			PIGEN_CONVERSION_EXACT_INTEGER);
		exact = pigen_expr_get(&model, right_operand->as.conversion.operand);
		assert(exact && exact->kind == PIGEN_EXPR_EXACT_INTEGER);
	}

	mixed_add = pigen_resolve_expression(&syntax, &model, scope,
		mixed_add_syntax, &semantic_error);
	known = pigen_expr_get(&model, mixed_add);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(resolved_width(&model, known->data_type) == 10);
	assert(pigen_expr_get(&model, known->as.binary.left)->kind ==
		PIGEN_EXPR_CONVERSION);
	assert(pigen_expr_get(&model, known->as.binary.left)->as.conversion.
		conversion.kind == PIGEN_CONVERSION_INTEGER_RESIZE);
	assert(pigen_expr_get(&model, known->as.binary.right)->kind ==
		PIGEN_EXPR_CONVERSION);
	assert(pigen_expr_get(&model, known->as.binary.right)->as.conversion.
		conversion.kind == PIGEN_CONVERSION_INTEGER_PROMOTION);

	integer_cast = pigen_resolve_expression(&syntax, &model, scope,
		integer_cast_syntax, &semantic_error);
	known = pigen_expr_get(&model, integer_cast);
	assert(known && known->kind == PIGEN_EXPR_CONVERSION);
	assert(known->as.conversion.conversion.kind ==
		PIGEN_CONVERSION_INTEGER_REINTERPRET);
	assert(known->data_type.index == unsigned_8.index);
	assert(pigen_lvalue_resolve(&model, integer_cast).index == PIGEN_INVALID_ID);

	byte_cast = pigen_resolve_expression(&syntax, &model, scope,
		byte_cast_syntax, &semantic_error);
	known = pigen_expr_get(&model, byte_cast);
	assert(known && known->kind == PIGEN_EXPR_CONVERSION);
	assert(known->as.conversion.conversion.kind ==
		PIGEN_CONVERSION_INTEGER_TO_VECTOR);

	identity_cast = pigen_resolve_expression(&syntax, &model, scope,
		identity_cast_syntax, &semantic_error);
	known = pigen_expr_get(&model, identity_cast);
	assert(known && known->kind == PIGEN_EXPR_SYMBOL);
	assert(known->as.symbol.index == unsigned_a.index);

	runtime_constant_cast = pigen_resolve_expression(&syntax, &model, scope,
		constant_cast_syntax, &semantic_error);
	constant_cast = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_cast_syntax, &semantic_error);
	known = pigen_expr_get(&model, runtime_constant_cast);
	assert(known && known->kind == PIGEN_EXPR_CONVERSION);
	assert(known->as.conversion.conversion.kind ==
		PIGEN_CONVERSION_VECTOR_TO_INTEGER);
	{
		const pigen_semantic_expr *constant_known = pigen_expr_get(&model,
			constant_cast);
		assert(constant_known && constant_known->kind == PIGEN_EXPR_CONVERSION);
		assert(constant_known->as.conversion.conversion.kind ==
			known->as.conversion.conversion.kind);
		assert(pigen_expr_get(&model, constant_known->as.conversion.operand)->kind ==
			PIGEN_EXPR_SYMBOL);
		assert(pigen_expr_get(&model, known->as.conversion.operand)->kind ==
			PIGEN_EXPR_SYMBOL);
	}
	{
		size_t expression_count = model.expression_count;
		const pigen_syntax_expr *cast_syntax = pigen_syntax_expr_get(
			&syntax.expressions, invalid_cast_syntax);
		semantic_error = (pigen_semantic_error){0};
		assert(pigen_resolve_expression(&syntax, &model, scope,
			invalid_cast_syntax, &semantic_error).index == PIGEN_INVALID_ID);
		assert(model.expression_count == expression_count);
		assert(cast_syntax);
		assert(semantic_error.span.start == cast_syntax->location.source_span.start);
		assert(semantic_error.span.end == cast_syntax->location.source_span.end);
	}

	{
		size_t expression_count = model.expression_count;
		const pigen_syntax_expr *invalid_syntax = pigen_syntax_expr_get(
			&syntax.expressions, invalid_mixed_syntax);
		semantic_error = (pigen_semantic_error){0};
		assert(pigen_resolve_expression(&syntax, &model, scope,
			invalid_mixed_syntax, &semantic_error).index == PIGEN_INVALID_ID);
		assert(model.expression_count == expression_count);
		assert(invalid_syntax);
		assert(semantic_error.span.start ==
			invalid_syntax->as.binary.operator_location.source_span.start);
		assert(semantic_error.span.end ==
			invalid_syntax->as.binary.operator_location.source_span.end);
	}

	runtime = pigen_resolve_expression(&syntax, &model, scope,
		runtime_syntax, NULL);
	known = pigen_expr_get(&model, runtime);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == unsized_integer_data_type.index);
	assert(known->as.binary.operation.operator == PIGEN_BINARY_ADD);
	assert(known->as.binary.operation.left_data_type.index ==
		unsized_integer_data_type.index);
	assert(known->as.binary.operation.right_data_type.index ==
		unsized_integer_data_type.index);
	assert(known->as.binary.operation.result_data_type.index ==
		unsized_integer_data_type.index);
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
		runtime_syntax, NULL).index == PIGEN_INVALID_ID);

	comparison = pigen_resolve_expression(&syntax, &model, scope,
		comparison_syntax, NULL);
	known = pigen_expr_get(&model, comparison);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == boolean_type.index);
	assert(known->as.binary.operation.operator == PIGEN_BINARY_EQUAL);
	assert(known->as.binary.operation.result_data_type.index ==
		boolean_type.index);
	assert(pigen_expr_constant(&model, comparison).index == PIGEN_INVALID_ID);

	constant = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_syntax, NULL);
	known = pigen_expr_get(&model, constant);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == unsized_integer_data_type.index);
	{
		const pigen_const_expr *constant_known = pigen_const_expr_get(&model,
			pigen_expr_constant(&model, constant));
		assert(constant_known &&
			constant_known->kind == PIGEN_CONST_EXPR_BINARY);
		assert(constant_known->as.binary.operation.operator ==
			PIGEN_BINARY_ADD);
		assert(constant_known->as.binary.operation.left_data_type.index ==
			known->as.binary.operation.left_data_type.index);
		assert(constant_known->as.binary.operation.right_data_type.index ==
			known->as.binary.operation.right_data_type.index);
		assert(constant_known->as.binary.operation.result_data_type.index ==
			known->as.binary.operation.result_data_type.index);
	}

	constant_index = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_index_syntax, NULL);
	known = pigen_expr_get(&model, constant_index);
	assert(known && known->kind == PIGEN_EXPR_INDEX);
	assert(known->data_type.index == boolean_type.index);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_index))->kind ==
		PIGEN_CONST_EXPR_INDEX);

	runtime_index = pigen_resolve_expression(&syntax, &model, scope,
		runtime_index_syntax, NULL);
	known = pigen_expr_get(&model, runtime_index);
	assert(known && known->kind == PIGEN_EXPR_INDEX);
	assert(known->data_type.index == boolean_type.index);
	assert(pigen_expr_constant(&model, runtime_index).index ==
		PIGEN_INVALID_ID);

	constant_range = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_range_syntax, NULL);
	known = pigen_expr_get(&model, constant_range);
	assert(known && known->kind == PIGEN_EXPR_SELECT);
	assert(known->as.select.kind == PIGEN_SEMANTIC_SELECT_RANGE);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_range))->kind ==
		PIGEN_CONST_EXPR_SELECT);

	constant_indexed = pigen_resolve_constant_expression(&syntax, &model,
		scope, constant_indexed_syntax, NULL);
	known = pigen_expr_get(&model, constant_indexed);
	assert(known && known->kind == PIGEN_EXPR_SELECT);
	assert(known->as.select.kind == PIGEN_SEMANTIC_SELECT_INDEXED_UP);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_indexed))->kind ==
		PIGEN_CONST_EXPR_SELECT);

	runtime_select = pigen_resolve_expression(&syntax, &model, scope,
		runtime_select_syntax, NULL);
	known = pigen_expr_get(&model, runtime_select);
	assert(known && known->kind == PIGEN_EXPR_SELECT);
	assert(known->as.select.kind == PIGEN_SEMANTIC_SELECT_INDEXED_DOWN);
	assert(pigen_expr_constant(&model, runtime_select).index ==
		PIGEN_INVALID_ID);
	assert(pigen_resolve_expression(&syntax, &model, scope,
		invalid_range_syntax, NULL).index == PIGEN_INVALID_ID);
	assert(pigen_resolve_expression(&syntax, &model, scope,
		invalid_width_syntax, NULL).index == PIGEN_INVALID_ID);

	constant_concat = pigen_resolve_constant_expression(&syntax, &model, scope,
		constant_concat_syntax, NULL);
	known = pigen_expr_get(&model, constant_concat);
	assert(known && known->kind == PIGEN_EXPR_CONCATENATION);
	assert(known->as.sequence.child_count == 2);
	assert(pigen_const_expr_get(&model,
		pigen_expr_constant(&model, constant_concat))->kind ==
		PIGEN_CONST_EXPR_CONCATENATION);

	runtime_concat = pigen_resolve_expression(&syntax, &model, scope,
		runtime_concat_syntax, NULL);
	swapped_concat = pigen_resolve_expression(&syntax, &model, scope,
		swapped_concat_syntax, NULL);
	known = pigen_expr_get(&model, runtime_concat);
	assert(known && known->kind == PIGEN_EXPR_CONCATENATION);
	assert(known->data_type.index ==
		pigen_expr_get(&model, swapped_concat)->data_type.index);
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
		aliased_add_syntax, NULL);
	known = pigen_expr_get(&model, aliased_add);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == aliased_type.index);
	aliased_compare = pigen_resolve_expression(&syntax, &model, scope,
		aliased_compare_syntax, NULL);
	known = pigen_expr_get(&model, aliased_compare);
	assert(known && known->kind == PIGEN_EXPR_BINARY);
	assert(known->data_type.index == boolean_type.index);

	pigen_free_semantic_model(&model);
	pigen_free_syntax_expr_arena(&syntax.expressions);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
	puts("PASS: runtime and constant expressions share typed resolution");
	return 0;
}
