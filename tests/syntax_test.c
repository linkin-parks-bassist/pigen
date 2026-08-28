#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/syntax.h"

typedef struct {
	pigen_preprocess_result preprocessed;
	pigen_syntax_tree tree;
	pigen_syntax_error error;
} parsed_fixture;

static int span_is(const pigen_source_manager *sources, pigen_source_span span,
	const char *expected)
{
	size_t length;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static int token_is(const pigen_expanded_source *source,
	pigen_token_id token, const char *expected)
{
	const pigen_expanded_token *known =
		pigen_expanded_token_get(source, token);
	const char *text;
	size_t length;
	if (!known) return 0;
	text = pigen_expanded_token_text(source, known, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static int expression_is(const pigen_source_manager *sources,
	const pigen_syntax_tree *tree, pigen_syntax_expr_id expression,
	const char *expected)
{
	const pigen_syntax_expr *known = pigen_syntax_expr_get(
		&tree->expressions, expression);
	return known && span_is(sources, known->location.source_span, expected);
}

static int parse_fixture(pigen_source_manager *sources, const char *name,
	const char *text, parsed_fixture *fixture)
{
	pigen_source_id source = pigen_source_add(sources, name, text, strlen(text));
	pigen_preprocess_error preprocess_error = {0};

	assert(source.index != PIGEN_INVALID_ID);
	assert(pigen_preprocess(sources, source, NULL, &fixture->preprocessed,
		&preprocess_error));
	return pigen_parse_syntax(&fixture->preprocessed.expanded, &fixture->tree,
		&fixture->error);
}

static void free_fixture(parsed_fixture *fixture)
{
	pigen_free_syntax_tree(&fixture->tree);
	pigen_free_preprocess_result(&fixture->preprocessed);
	*fixture = (parsed_fixture){0};
}

static const pigen_syntax_node *fixture_module(const parsed_fixture *fixture)
{
	const pigen_syntax_node *root = pigen_syntax_get(&fixture->tree,
		(pigen_syntax_id){0});
	pigen_syntax_id child;

	assert(root && root->kind == PIGEN_SYNTAX_COMPILATION_UNIT);
	for (child = root->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_MODULE) return node;
		child = node->next_sibling;
	}
	return NULL;
}

static const pigen_syntax_node *find_declarator(const parsed_fixture *fixture,
	const pigen_syntax_node *module, const char *name,
	const pigen_syntax_node **declaration)
{
	pigen_syntax_id child;

	for (child = module->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);
		pigen_syntax_id declarator;
		assert(node);
		if (node->kind != PIGEN_SYNTAX_SIGNAL_DECLARATION)
		{
			child = node->next_sibling;
			continue;
		}
		for (declarator = node->first_child;
			declarator.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *known = pigen_syntax_get(&fixture->tree,
				declarator);
			assert(known && known->kind == PIGEN_SYNTAX_SIGNAL_DECLARATOR);
			if (token_is(&fixture->preprocessed.expanded,
				known->as.signal_declarator.name, name))
			{
				if (declaration) *declaration = node;
				return known;
			}
			declarator = known->next_sibling;
		}
		child = node->next_sibling;
	}
	return NULL;
}

static void assert_opaque_without_arena_leakage(pigen_source_manager *sources,
	const char *name, const char *text)
{
	parsed_fixture fixture = {0};
	const pigen_syntax_node *module;
	const pigen_syntax_node *opaque;

	assert(parse_fixture(sources, name, text, &fixture));
	assert(!fixture.error.message);
	module = fixture_module(&fixture);
	assert(module);
	opaque = pigen_syntax_get(&fixture.tree, module->first_child);
	assert(opaque && opaque->kind == PIGEN_SYNTAX_OPAQUE);
	assert(opaque->next_sibling.index == PIGEN_INVALID_ID);
	assert(span_is(sources, opaque->location.source_span, text));
	assert(fixture.tree.node_count == 3);
	assert(fixture.tree.expressions.node_count == 0);
	assert(fixture.tree.expressions.child_count == 0);
	assert(fixture.tree.types.node_count == 0);
	assert(fixture.tree.types.argument_count == 0);
	assert(fixture.tree.shape_dimension_count == 0);
	free_fixture(&fixture);
}

static void assert_rejected_at(pigen_source_manager *sources, const char *name,
	const char *text, const char *expected_span)
{
	parsed_fixture fixture = {0};

	assert(!parse_fixture(sources, name, text, &fixture));
	assert(fixture.error.message);
	assert(span_is(sources, fixture.error.span, expected_span));
	free_fixture(&fixture);
}

static void test_cast_syntax(pigen_source_manager *sources)
{
	const char text[] =
		"uint[8]'(value) word_t'(value) logic [15:0]'(value) "
		"uint[]'(value) logic[15:]'(value) uint[8]'value (value)'(value)";
	pigen_source_id source = pigen_source_add(sources, "casts.pigen", text,
		strlen(text));
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_expr_arena expressions = {0};
	pigen_syntax_type_arena types = {0};
	pigen_syntax_expr_id expression;
	pigen_syntax_error error = {0};
	const pigen_syntax_expr *node;
	const pigen_syntax_type *type;
	const pigen_syntax_type_argument *argument;

	assert(pigen_preprocess(sources, source, NULL, &preprocessed,
		&preprocess_error));
	assert(pigen_parse_expression(&preprocessed.expanded, 0, 8,
		&expressions, &types, &expression, &error));
	node = pigen_syntax_expr_get(&expressions, expression);
	assert(node && node->kind == PIGEN_SYNTAX_EXPR_CAST);
	type = pigen_syntax_type_get(&types, node->as.cast.type);
	assert(type && type->argument_count == 1);
	argument = pigen_syntax_type_arguments(&types, type->first_argument,
		type->argument_count);
	assert(argument && argument->kind == PIGEN_SYNTAX_TYPE_COUNT);
	assert(span_is(sources, node->location.source_span, "uint[8]'(value)"));
	assert(pigen_parse_expression(&preprocessed.expanded, 8, 13,
		&expressions, &types, &expression, &error));
	node = pigen_syntax_expr_get(&expressions, expression);
	type = pigen_syntax_type_get(&types, node->as.cast.type);
	assert(type && !type->argument_count &&
		token_is(&preprocessed.expanded, type->base, "word_t"));
	assert(pigen_parse_expression(&preprocessed.expanded, 13, 23,
		&expressions, &types, &expression, &error));
	node = pigen_syntax_expr_get(&expressions, expression);
	type = pigen_syntax_type_get(&types, node->as.cast.type);
	argument = pigen_syntax_type_arguments(&types, type->first_argument,
		type->argument_count);
	assert(type && type->argument_count == 1 && argument &&
		argument->kind == PIGEN_SYNTAX_TYPE_RANGE);
	assert(!pigen_parse_expression(&preprocessed.expanded, 23, 30,
		&expressions, &types, &expression, &error));
	assert(!pigen_parse_expression(&preprocessed.expanded, 30, 39,
		&expressions, &types, &expression, &error));
	assert(!pigen_parse_expression(&preprocessed.expanded, 39, 45,
		&expressions, &types, &expression, &error));
	assert(!pigen_parse_expression(&preprocessed.expanded, 45, 52,
		&expressions, &types, &expression, &error));
	pigen_free_syntax_expr_arena(&expressions);
	pigen_free_syntax_type_arena(&types);
	pigen_free_preprocess_result(&preprocessed);
}

static void test_data_first_declarations(pigen_source_manager *sources)
{
	const char text[] =
		"module declarations #(\n"
		"\tparameter DEPTH = 8,\n"
		"\tparameter LANES = 2\n"
		") (\n"
		"\tinput int[16] sample,\n"
		"\tinput uint[8] opcode,\n"
		"\tinput bit flag,\n"
		"\tinput int[16] buf queued_sample,\n"
		"\toutput int[16] buf result,\n"
		"\toutput logic [7:0] ordinary,\n"
		"\tinput bit[8] wire ansi_scalar, ansi_count[LANES], ansi_range[3:0]\n"
		");\n"
		"\ttypedef bit[8] packet_t;\n"
		"\tint[16] buf left, right;\n"
		"\tbit[8] wire mask;\n"
		"\tpacket_t fifo[DEPTH] queue[LANES], alternate[3:0];\n"
		"\tbit logic tag;\n"
		"\twire [7:0] adapter_wire;\n"
		"\toutput reg adapter_reg;\n"
		"\toutput [7:0] adapter_implicit;\n"
		"\talways @(posedge flag) begin\n"
		"\t\tright <= left;\n"
		"\tend\n"
		"endmodule\n";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *module;
	const pigen_syntax_node *declaration;
	const pigen_syntax_node *declarator;
	const pigen_syntax_node *queue_declaration;
	const pigen_syntax_type *type;
	const pigen_syntax_type_argument *type_argument;
	const pigen_syntax_shape_dimension *shape;
	pigen_syntax_id child;
	size_t declaration_count = 0;
	size_t declarator_count = 0;
	size_t clocked_process_count = 0;

	assert(parse_fixture(sources, "declarations.pigen", text, &fixture));
	module = fixture_module(&fixture);
	assert(module && token_is(&fixture.preprocessed.expanded,
		module->as.module.name, "declarations"));
	for (child = module->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture.tree, child);
		pigen_syntax_id declarator_id;
		assert(node);
		if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION)
		{
			declaration_count++;
			for (declarator_id = node->first_child;
				declarator_id.index != PIGEN_INVALID_ID; )
			{
				const pigen_syntax_node *known = pigen_syntax_get(&fixture.tree,
					declarator_id);
				assert(known && known->kind == PIGEN_SYNTAX_SIGNAL_DECLARATOR);
				assert(known->parent.index == child.index);
				declarator_count++;
				declarator_id = known->next_sibling;
			}
		}
		else if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS)
		{
			const pigen_syntax_node *block = pigen_syntax_get(&fixture.tree,
				node->first_child);
			const pigen_syntax_node *assignment;
			assert(block && block->kind == PIGEN_SYNTAX_PROCEDURAL_BLOCK);
			assignment = pigen_syntax_get(&fixture.tree, block->first_child);
			assert(assignment && assignment->kind ==
				PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			assert(expression_is(sources, &fixture.tree,
				assignment->as.nonblocking_assignment.destination, "right"));
			assert(expression_is(sources, &fixture.tree,
				assignment->as.nonblocking_assignment.value, "left"));
			clocked_process_count++;
		}
		child = node->next_sibling;
	}
	assert(declaration_count == 14);
	assert(declarator_count == 18);
	assert(clocked_process_count == 1);

	declarator = find_declarator(&fixture, module, "left", &declaration);
	assert(declarator && declaration);
	type = pigen_syntax_type_get(&fixture.tree.types,
		declaration->as.signal_declaration.data_type);
	assert(type && token_is(&fixture.preprocessed.expanded, type->base, "int"));
	assert(type->argument_count == 1);
	type_argument = pigen_syntax_type_arguments(&fixture.tree.types,
		type->first_argument, type->argument_count);
	assert(type_argument && type_argument->kind == PIGEN_SYNTAX_TYPE_COUNT);
	assert(expression_is(sources, &fixture.tree, type_argument->as.count, "16"));
	assert(declaration->as.signal_declaration.has_transfer_type);
	assert(declaration->as.signal_declaration.transfer_type.transfer_type ==
		PIGEN_TRANSFER_TYPE_BUF);
	assert(declaration->as.signal_declaration.transfer_type.argument.index ==
		PIGEN_INVALID_ID);

	declarator = find_declarator(&fixture, module, "queue", &queue_declaration);
	assert(declarator && queue_declaration);
	assert(queue_declaration->as.signal_declaration.has_transfer_type);
	assert(queue_declaration->as.signal_declaration.transfer_type.transfer_type ==
		PIGEN_TRANSFER_TYPE_FIFO);
	assert(span_is(sources, queue_declaration->as.signal_declaration.
		transfer_type.location.source_span, "fifo"));
	assert(expression_is(sources, &fixture.tree, queue_declaration->as.
		signal_declaration.transfer_type.argument, "DEPTH"));
	assert(pigen_syntax_expr_get(&fixture.tree.expressions,
		queue_declaration->as.signal_declaration.transfer_type.argument)->
		location.source_span.start != queue_declaration->as.signal_declaration.
		transfer_type.location.source_span.start);
	shape = pigen_syntax_declarator_shape_dimensions(&fixture.tree, declarator);
	assert(shape && declarator->as.signal_declarator.dimension_count == 1);
	assert(shape[0].form == PIGEN_SYNTAX_SHAPE_DIMENSION_COUNT);
	assert(expression_is(sources, &fixture.tree, shape[0].as.count, "LANES"));
	declarator = find_declarator(&fixture, module, "alternate", &declaration);
	assert(declarator && declaration == queue_declaration);
	shape = pigen_syntax_declarator_shape_dimensions(&fixture.tree, declarator);
	assert(shape && declarator->as.signal_declarator.dimension_count == 1);
	assert(shape[0].form == PIGEN_SYNTAX_SHAPE_DIMENSION_RANGE);
	assert(expression_is(sources, &fixture.tree, shape[0].as.range.left, "3"));
	assert(expression_is(sources, &fixture.tree, shape[0].as.range.right, "0"));

	declarator = find_declarator(&fixture, module, "sample", &declaration);
	assert(declarator && declaration &&
		!declaration->as.signal_declaration.has_transfer_type);
	assert(declaration->as.signal_declaration.transfer_type.argument.index ==
		PIGEN_INVALID_ID);
	declarator = find_declarator(&fixture, module, "opcode", &declaration);
	assert(declarator && declaration &&
		!declaration->as.signal_declaration.has_transfer_type);
	assert(declaration->as.signal_declaration.transfer_type.argument.index ==
		PIGEN_INVALID_ID);
	declarator = find_declarator(&fixture, module, "flag", &declaration);
	assert(declarator && declaration &&
		!declaration->as.signal_declaration.has_transfer_type);
	assert(declaration->as.signal_declaration.transfer_type.argument.index ==
		PIGEN_INVALID_ID);

	declarator = find_declarator(&fixture, module, "ansi_scalar", &declaration);
	assert(declarator && declaration);
	assert(declarator->as.signal_declarator.dimension_count == 0);
	declarator = pigen_syntax_get(&fixture.tree, declarator->next_sibling);
	assert(declarator && token_is(&fixture.preprocessed.expanded,
		declarator->as.signal_declarator.name, "ansi_count"));
	shape = pigen_syntax_declarator_shape_dimensions(&fixture.tree, declarator);
	assert(shape && shape[0].form == PIGEN_SYNTAX_SHAPE_DIMENSION_COUNT);
	assert(expression_is(sources, &fixture.tree, shape[0].as.count, "LANES"));
	declarator = pigen_syntax_get(&fixture.tree, declarator->next_sibling);
	assert(declarator && token_is(&fixture.preprocessed.expanded,
		declarator->as.signal_declarator.name, "ansi_range"));
	shape = pigen_syntax_declarator_shape_dimensions(&fixture.tree, declarator);
	assert(shape && shape[0].form == PIGEN_SYNTAX_SHAPE_DIMENSION_RANGE);
	assert(expression_is(sources, &fixture.tree, shape[0].as.range.left, "3"));
	assert(expression_is(sources, &fixture.tree, shape[0].as.range.right, "0"));
	assert(declarator->next_sibling.index == PIGEN_INVALID_ID);

	declarator = find_declarator(&fixture, module, "adapter_wire", &declaration);
	assert(declarator && declaration &&
		declaration->as.signal_declaration.has_transfer_type);
	assert(declaration->as.signal_declaration.transfer_type.transfer_type ==
		PIGEN_TRANSFER_TYPE_WIRE);
	assert(span_is(sources, declaration->as.signal_declaration.transfer_type.
		location.source_span, "wire"));
	type = pigen_syntax_type_get(&fixture.tree.types,
		declaration->as.signal_declaration.data_type);
	assert(type && type->base.index == PIGEN_INVALID_ID &&
		type->argument_count == 1);
	type_argument = pigen_syntax_type_arguments(&fixture.tree.types,
		type->first_argument, type->argument_count);
	assert(type_argument && type_argument->kind == PIGEN_SYNTAX_TYPE_RANGE);
	declarator = find_declarator(&fixture, module, "adapter_reg", &declaration);
	assert(declarator && declaration &&
		declaration->as.signal_declaration.has_transfer_type);
	assert(declaration->as.signal_declaration.transfer_type.transfer_type ==
		PIGEN_TRANSFER_TYPE_REG);
	type = pigen_syntax_type_get(&fixture.tree.types,
		declaration->as.signal_declaration.data_type);
	assert(type && type->base.index == PIGEN_INVALID_ID &&
		type->argument_count == 0);
	declarator = find_declarator(&fixture, module, "adapter_implicit",
		&declaration);
	assert(declarator && declaration &&
		!declaration->as.signal_declaration.has_transfer_type);
	type = pigen_syntax_type_get(&fixture.tree.types,
		declaration->as.signal_declaration.data_type);
	assert(type && type->base.index == PIGEN_INVALID_ID &&
		type->argument_count == 1);
	type_argument = pigen_syntax_type_arguments(&fixture.tree.types,
		type->first_argument, type->argument_count);
	assert(type_argument && type_argument->kind == PIGEN_SYNTAX_TYPE_RANGE);

	free_fixture(&fixture);
}

static void test_transactional_and_clean_break_syntax(
	pigen_source_manager *sources)
{
	assert_opaque_without_arena_leakage(sources, "ordinary_byte.pigen",
		"module ordinary_byte; byte ordinary_value; endmodule");
	assert_opaque_without_arena_leakage(sources, "ordinary_initializer.pigen",
		"module ordinary_initializer; logic [{WIDTH, LANES + 1}:0] "
		"ordinary[LANES + 1] = 8'h5a; endmodule");
	assert_rejected_at(sources, "old_order.pigen",
		"module old_order; buf int[16] old_order; endmodule", "buf");
	assert_rejected_at(sources, "pigen_initializer.pigen",
		"module pigen_initializer; int[16] buf value = 0; endmodule", "=");
	assert_rejected_at(sources, "missing_depth.pigen",
		"module missing_depth; packet_t fifo queue; endmodule", "fifo");
	assert_rejected_at(sources, "empty_depth.pigen",
		"module empty_depth; packet_t fifo[] queue; endmodule", "[");
	assert_rejected_at(sources, "repeated_depth.pigen",
		"module repeated_depth; packet_t fifo[4][2] queue; endmodule", "[");
	assert_rejected_at(sources, "unexpected_transfer_argument.pigen",
		"module unexpected_transfer_argument; bit[8] buf[2] value; endmodule",
		"[");
	assert_rejected_at(sources, "unknown_transfer.pigen",
		"module unknown_transfer; bit[8] mystery value; endmodule", "value");
	assert_rejected_at(sources, "directionless_dynamic_port.pigen",
		"module directionless_dynamic_port(bit[8] buf value); endmodule", "bit");
}

static void test_process_rollback(pigen_source_manager *sources)
{
	const char text[] =
		"module rollback(input logic clk);\n"
		"  bit buf left, right;\n"
		"  always @(posedge clk) begin\n"
		"    right <= left;\n"
		"    left <= right + ;\n"
		"  end\n"
		"endmodule";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *module;
	pigen_syntax_id child;
	size_t clocked_process_count = 0;

	assert(parse_fixture(sources, "rollback.pigen", text, &fixture));
	module = fixture_module(&fixture);
	assert(module);
	for (child = module->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture.tree, child);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS)
			clocked_process_count++;
		child = node->next_sibling;
	}
	assert(!clocked_process_count);
	assert(fixture.tree.node_count == 10);
	assert(fixture.tree.expressions.node_count == 0);
	assert(fixture.tree.expressions.child_count == 0);
	assert(fixture.tree.types.node_count == 2);
	assert(fixture.tree.types.argument_count == 0);
	assert(fixture.tree.shape_dimension_count == 0);
	assert(fixture.error.origin.index == PIGEN_INVALID_ID);
	assert(fixture.error.span.source.index == PIGEN_INVALID_ID);
	assert(!fixture.error.message);
	free_fixture(&fixture);
}

static void test_missing_pigen_terminator(pigen_source_manager *sources)
{
	assert_rejected_at(sources, "missing_terminator.pigen",
		"module missing_terminator; int[16] buf value endmodule", "endmodule");
}

int main(void)
{
	pigen_source_manager sources = {0};

	test_cast_syntax(&sources);
	test_data_first_declarations(&sources);
	test_transactional_and_clean_break_syntax(&sources);
	test_process_rollback(&sources);
	test_missing_pigen_terminator(&sources);
	pigen_free_sources(&sources);
	puts("PASS: data-first declarations use one transactional syntax topology");
	return 0;
}
