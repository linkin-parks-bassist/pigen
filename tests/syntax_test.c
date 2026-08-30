#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/syntax.h"

typedef struct {
	pigen_source_id source;
	pigen_preprocess_result preprocessed;
	pigen_syntax_tree tree;
	pigen_syntax_error error;
} parsed_fixture;

typedef struct {
	size_t syntax_nodes;
	size_t expression_nodes;
	size_t expression_children;
	size_t type_nodes;
	size_t type_arguments;
	size_t shape_dimensions;
} syntax_arena_counts;

typedef struct {
	const char *name;
	const char *source;
	const char *message;
	const char *span;
	size_t span_occurrence;
} syntax_failure;

static int span_is(const pigen_source_manager *sources, pigen_source_span span,
	const char *expected)
{
	size_t length;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static size_t source_occurrence_start(const char *source, const char *expected,
	size_t occurrence)
{
	const char *at = source;
	size_t i;

	assert(occurrence);
	for (i = 0; i < occurrence; i++)
	{
		at = strstr(at, expected);
		assert(at);
		if (i + 1 < occurrence) at++;
	}
	return (size_t)(at - source);
}

static int span_is_source_occurrence(const pigen_source_manager *sources,
	pigen_source_span span, pigen_source_id source_id, const char *source,
	const char *expected, size_t occurrence)
{
	size_t start = source_occurrence_start(source, expected, occurrence);

	return span.source.index == source_id.index &&
		span_is(sources, span, expected) && span.start == start &&
		span.end == start + strlen(expected);
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
	fixture->source = source;
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

static syntax_arena_counts snapshot_syntax_arenas(
	const pigen_syntax_tree *tree)
{
	return (syntax_arena_counts){
		.syntax_nodes = tree->node_count,
		.expression_nodes = tree->expressions.node_count,
		.expression_children = tree->expressions.child_count,
		.type_nodes = tree->types.node_count,
		.type_arguments = tree->types.argument_count,
		.shape_dimensions = tree->shape_dimension_count
	};
}

static void assert_syntax_arena_counts(syntax_arena_counts actual,
	syntax_arena_counts expected)
{
	assert(actual.syntax_nodes == expected.syntax_nodes);
	assert(actual.expression_nodes == expected.expression_nodes);
	assert(actual.expression_children == expected.expression_children);
	assert(actual.type_nodes == expected.type_nodes);
	assert(actual.type_arguments == expected.type_arguments);
	assert(actual.shape_dimensions == expected.shape_dimensions);
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
	const syntax_arena_counts expected = {.syntax_nodes = 3};

	assert(parse_fixture(sources, name, text, &fixture));
	assert(!fixture.error.message);
	module = fixture_module(&fixture);
	assert(module);
	opaque = pigen_syntax_get(&fixture.tree, module->first_child);
	if (!opaque || opaque->kind != PIGEN_SYNTAX_OPAQUE ||
		opaque->next_sibling.index != PIGEN_INVALID_ID)
		fprintf(stderr, "expected `%s` to remain one opaque syntax region\n", name);
	assert(opaque && opaque->kind == PIGEN_SYNTAX_OPAQUE);
	assert(opaque->next_sibling.index == PIGEN_INVALID_ID);
	assert(span_is(sources, opaque->location.source_span, text));
	assert_syntax_arena_counts(snapshot_syntax_arenas(&fixture.tree), expected);
	free_fixture(&fixture);
}

static void assert_compilation_unit_opaque_without_arena_leakage(
	pigen_source_manager *sources, const char *name, const char *text)
{
	parsed_fixture fixture = {0};
	const pigen_syntax_node *root;
	const pigen_syntax_node *opaque;
	const syntax_arena_counts expected = {.syntax_nodes = 2};

	assert(parse_fixture(sources, name, text, &fixture));
	assert(!fixture.error.message);
	root = pigen_syntax_get(&fixture.tree, (pigen_syntax_id){0});
	assert(root && root->kind == PIGEN_SYNTAX_COMPILATION_UNIT);
	opaque = pigen_syntax_get(&fixture.tree, root->first_child);
	if (!opaque || opaque->kind != PIGEN_SYNTAX_OPAQUE ||
		opaque->next_sibling.index != PIGEN_INVALID_ID)
		fprintf(stderr, "expected `%s` to remain one compilation-unit opaque "
			"syntax region\n", name);
	assert(opaque && opaque->kind == PIGEN_SYNTAX_OPAQUE);
	assert(opaque->next_sibling.index == PIGEN_INVALID_ID);
	assert(span_is(sources, opaque->location.source_span, text));
	assert_syntax_arena_counts(snapshot_syntax_arenas(&fixture.tree), expected);
	free_fixture(&fixture);
}

static void assert_syntax_failure(pigen_source_manager *sources,
	syntax_failure failure)
{
	parsed_fixture fixture = {0};
	size_t expected_start = source_occurrence_start(failure.source, failure.span,
		failure.span_occurrence);
	int parsed = parse_fixture(sources, failure.name, failure.source, &fixture);

	if (parsed)
		fprintf(stderr, "expected `%s` to fail syntax parsing\n", failure.name);
	assert(!parsed);
	if (!fixture.error.message || strcmp(fixture.error.message, failure.message) ||
		!span_is_source_occurrence(sources, fixture.error.span, fixture.source,
			failure.source, failure.span, failure.span_occurrence))
		fprintf(stderr,
			"expected `%s` at `%s` occurrence %zu, byte range %zu:%zu; "
			"got `%s` at byte range %zu:%zu\n",
			failure.message, failure.span, failure.span_occurrence, expected_start,
			expected_start + strlen(failure.span),
			fixture.error.message ? fixture.error.message : "(none)",
			fixture.error.span.start, fixture.error.span.end);
	assert(fixture.error.message &&
		!strcmp(fixture.error.message, failure.message));
	assert(span_is_source_occurrence(sources, fixture.error.span, fixture.source,
		failure.source, failure.span, failure.span_occurrence));
	free_fixture(&fixture);
}

static void assert_syntax_eof_failure(pigen_source_manager *sources,
	const char *name, const char *source, const char *message)
{
	parsed_fixture fixture = {0};
	size_t boundary = strlen(source);
	int parsed = parse_fixture(sources, name, source, &fixture);

	if (parsed)
		fprintf(stderr, "expected `%s` to fail syntax parsing\n", name);
	assert(!parsed);
	if (!fixture.error.message || strcmp(fixture.error.message, message) ||
		fixture.error.span.source.index != fixture.source.index ||
		fixture.error.span.start != boundary ||
		fixture.error.span.end != boundary)
		fprintf(stderr,
			"expected `%s` at end of source, byte %zu; got `%s` at byte "
			"range %zu:%zu\n",
			message, boundary,
			fixture.error.message ? fixture.error.message : "(none)",
			fixture.error.span.start, fixture.error.span.end);
	assert(fixture.error.message && !strcmp(fixture.error.message, message));
	assert(fixture.error.span.source.index == fixture.source.index);
	assert(fixture.error.span.start == boundary);
	assert(fixture.error.span.end == boundary);
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

static void test_ordinary_systemverilog_preservation_matrix(
	pigen_source_manager *sources)
{
	const char text[] =
		"module ordinary_preservation;\n"
		"  wire scalar_wire;\n"
		"  wire [7:0] ranged_wire;\n"
		"  reg signed [15:0] variable;\n"
		"  logic [3:0] internal_logic;\n"
		"  bit internal_bit;\n"
		"  input wire [7:0] input_wire;\n"
		"  input logic [7:0] input_logic;\n"
		"  output reg [7:0] output_reg;\n"
		"  output logic [7:0] output_logic;\n"
		"  inout wire bidirectional;\n"
		"endmodule\n";
	typedef struct {
		const char *name;
		int has_written_transfer;
		pigen_transfer_type transfer_type;
	} ordinary_expectation;
	static const ordinary_expectation expectations[] = {
		{"scalar_wire", 1, PIGEN_TRANSFER_TYPE_WIRE},
		{"ranged_wire", 1, PIGEN_TRANSFER_TYPE_WIRE},
		{"variable", 1, PIGEN_TRANSFER_TYPE_REG},
		{"internal_logic", 0, PIGEN_TRANSFER_TYPE_ABSTRACT},
		{"internal_bit", 0, PIGEN_TRANSFER_TYPE_ABSTRACT},
		{"input_wire", 1, PIGEN_TRANSFER_TYPE_WIRE},
		{"input_logic", 0, PIGEN_TRANSFER_TYPE_ABSTRACT},
		{"output_reg", 1, PIGEN_TRANSFER_TYPE_REG},
		{"output_logic", 0, PIGEN_TRANSFER_TYPE_ABSTRACT},
		{"bidirectional", 1, PIGEN_TRANSFER_TYPE_WIRE}
	};
	parsed_fixture fixture = {0};
	const pigen_syntax_node *module;
	size_t i;

	assert(parse_fixture(sources, "ordinary_preservation.sv", text, &fixture));
	assert(!fixture.error.message);
	module = fixture_module(&fixture);
	assert(module);
	for (i = 0; i < sizeof(expectations) / sizeof(*expectations); i++)
	{
		const ordinary_expectation *expected = &expectations[i];
		const pigen_syntax_node *declaration;
		const pigen_syntax_node *declarator = find_declarator(&fixture, module,
			expected->name, &declaration);

		assert(declarator && declaration);
		assert(declaration->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION);
		assert(declarator->kind == PIGEN_SYNTAX_SIGNAL_DECLARATOR);
		assert(declarator->parent.index ==
			(size_t)(declaration - fixture.tree.nodes));
		assert(declaration->as.signal_declaration.has_transfer_type ==
			expected->has_written_transfer);
		if (expected->has_written_transfer)
			assert(declaration->as.signal_declaration.transfer_type.transfer_type ==
				expected->transfer_type);
	}
	free_fixture(&fixture);
}

static void test_contextual_leading_transfer_spelling(
	pigen_source_manager *sources)
{
	const char ansi_text[] =
		"module ambiguous_ports(input buf, output port); endmodule";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *module;
	const pigen_syntax_node *declaration;

	assert_opaque_without_arena_leakage(sources, "ordinary_buf_gate.sv",
		"module ordinary_buf_gate; "
		"buf gate_instance(output_signal, input_signal); endmodule");
	assert_syntax_failure(sources, (syntax_failure){
		"transfer_first.pigen",
		"module transfer_first; buf int[16] old_order; endmodule",
		"data type must precede the transfer type", "buf", 1});
	assert_syntax_failure(sources, (syntax_failure){
		"unterminated_transfer_first.pigen",
		"module unterminated_transfer_first; buf int[16] old_order endmodule",
		"data type must precede the transfer type", "buf", 1});
	assert_opaque_without_arena_leakage(sources, "unterminated_buf_gate.sv",
		"module unterminated_buf_gate; "
		"buf gate_instance(output_signal, input_signal) endmodule");
	assert(parse_fixture(sources, "ambiguous_ports.sv", ansi_text, &fixture));
	assert(!fixture.error.message);
	module = fixture_module(&fixture);
	assert(module);
	assert(find_declarator(&fixture, module, "buf", &declaration));
	assert(declaration && !declaration->as.signal_declaration.has_transfer_type);
	assert(find_declarator(&fixture, module, "port", &declaration));
	assert(declaration && !declaration->as.signal_declaration.has_transfer_type);
	free_fixture(&fixture);
}

static void test_affirmative_declaration_ownership(
	pigen_source_manager *sources)
{
	const char alias_text[] =
		"typedef int[16] sample_t; "
		"module alias_port(input sample_t endpoint); endmodule";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *module;
	const pigen_syntax_node *declaration;

	assert_opaque_without_arena_leakage(sources, "ordinary_int.sv",
		"module ordinary_int; int value; endmodule");
	assert_opaque_without_arena_leakage(sources, "ordinary_interface.sv",
		"module ordinary_interface(input bus_if endpoint); endmodule");
	assert(parse_fixture(sources, "structured_alias.pigen", alias_text,
		&fixture));
	assert(!fixture.error.message);
	module = fixture_module(&fixture);
	assert(module);
	assert(find_declarator(&fixture, module, "endpoint", &declaration));
	assert(declaration && declaration->as.signal_declaration.direction ==
		PIGEN_DIRECTION_INPUT);
	assert(!declaration->as.signal_declaration.has_transfer_type);
	free_fixture(&fixture);
}

static void test_opaque_typedef_shadow_barrier(pigen_source_manager *sources)
{
	const char text[] =
		"typedef int[16] sample_t;\n"
		"typedef int[8] other_t;\n"
		"module opaque_shadow;\n"
		"  input sample_t before_shadow;\n"
		"  typedef byte sample_t;\n"
		"  input sample_t after_shadow;\n"
		"  input other_t unrelated;\n"
		"endmodule\n";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *module;
	const pigen_syntax_node *declaration;

	assert(parse_fixture(sources, "opaque_typedef_shadow.sv", text, &fixture));
	assert(!fixture.error.message);
	module = fixture_module(&fixture);
	assert(module);
	assert(find_declarator(&fixture, module, "before_shadow", &declaration));
	assert(declaration && declaration->as.signal_declaration.direction ==
		PIGEN_DIRECTION_INPUT);
	assert(!find_declarator(&fixture, module, "after_shadow", NULL));
	assert(find_declarator(&fixture, module, "unrelated", &declaration));
	assert(declaration && declaration->as.signal_declaration.direction ==
		PIGEN_DIRECTION_INPUT);
	free_fixture(&fixture);
}

static void test_transactional_typedef_ownership(
	pigen_source_manager *sources)
{
	const char opaque_alias_text[] =
		"typedef byte byte_t; "
		"module opaque_alias(input byte_t endpoint); endmodule";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *module;
	const syntax_arena_counts opaque_alias_counts = {.syntax_nodes = 4};

	assert_compilation_unit_opaque_without_arena_leakage(sources,
		"ordinary_byte_typedef.sv", "typedef byte byte_t;");
	assert_opaque_without_arena_leakage(sources,
		"module_byte_typedef.sv",
		"module module_byte_typedef; typedef byte byte_t; endmodule");
	assert_compilation_unit_opaque_without_arena_leakage(sources,
		"ordinary_aggregate_typedef.sv",
		"typedef struct packed { logic [3:0] tag; logic flag; } packet_t;");
	assert_compilation_unit_opaque_without_arena_leakage(sources,
		"ordinary_array_typedef.sv",
		"typedef logic [3:0] word_t [0:1];");
	assert_opaque_without_arena_leakage(sources,
		"module_array_typedef.sv",
		"module module_array_typedef; "
		"typedef logic [3:0] word_t [0:1]; endmodule");
	assert(parse_fixture(sources, "opaque_alias_name.sv", opaque_alias_text,
		&fixture));
	assert(!fixture.error.message);
	module = fixture_module(&fixture);
	assert(module);
	assert(!find_declarator(&fixture, module, "endpoint", NULL));
	assert_syntax_arena_counts(snapshot_syntax_arenas(&fixture.tree),
		opaque_alias_counts);
	free_fixture(&fixture);
	assert_syntax_failure(sources, (syntax_failure){
		"malformed_pigen_typedef.pigen", "typedef int[16] ;",
		"typedef requires a name", ";", 1});
	assert_syntax_failure(sources, (syntax_failure){
		"unsupported_pigen_typedef_shape.pigen",
		"typedef int[16] sample_t [0:1];",
		"typedef permits exactly one name", "[", 2});
}

static void test_transactional_and_clean_break_syntax(
	pigen_source_manager *sources)
{
	typedef struct {
		const char *name;
		const char *source;
	} opaque_case;
	static const opaque_case opaque_cases[] = {
		{"ordinary_byte.sv",
			"module ordinary_byte; byte ordinary_value; endmodule"},
		{"ordinary_interface_port.sv",
			"module ordinary_interface_port(input bus_if.master endpoint); "
			"endmodule"},
		{"ordinary_initializer.sv",
			"module ordinary_initializer; logic [{WIDTH, LANES + 1}:0] "
			"ordinary[LANES + 1] = 8'h5a; endmodule"},
		{"ordinary_aggregate.sv",
			"module ordinary_aggregate; struct packed { logic [3:0] tag; "
			"logic flag; } packet; endmodule"}
	};
	static const syntax_failure failures[] = {
		{"missing_depth.pigen",
			"module missing_depth; packet_t fifo queue; endmodule",
			"transfer type requires a depth argument", "fifo", 1},
		{"empty_depth.pigen",
			"module empty_depth; packet_t fifo[] queue; endmodule",
			"transfer depth requires an expression", "[", 1},
		{"repeated_depth.pigen",
			"module repeated_depth; packet_t fifo[4][2] queue; endmodule",
			"transfer type accepts exactly one argument", "[", 2},
		{"unexpected_transfer_argument.pigen",
			"module unexpected_transfer_argument; bit[8] buf[2] value; "
			"endmodule", "transfer type does not accept an argument", "[", 2},
		{"missing_declarator.pigen",
			"module missing_declarator; bit[8] buf; endmodule",
			"declaration requires a signal name", ";", 2},
		{"invalid_continuation.pigen",
			"module invalid_continuation; bit[8] buf first, second + third; "
			"endmodule",
			"expected `,` or declaration terminator after signal name", "+", 1},
		{"pigen_initializer.pigen",
			"module pigen_initializer; int[16] buf value = 0; endmodule",
			"expected `,` or declaration terminator after signal name", "=", 1},
		{"unknown_transfer.pigen",
			"module unknown_transfer; bit[8] mystery value; endmodule",
			"expected `,` or declaration terminator after signal name", "value", 1},
		{"directionless_dynamic_port.pigen",
			"module directionless_dynamic_port(bit[8] buf value); endmodule",
			"ANSI dynamic signal port requires `input` or `output`", "bit", 1}
	};
	size_t i;

	for (i = 0; i < sizeof(opaque_cases) / sizeof(*opaque_cases); i++)
		assert_opaque_without_arena_leakage(sources, opaque_cases[i].name,
			opaque_cases[i].source);
	for (i = 0; i < sizeof(failures) / sizeof(*failures); i++)
		assert_syntax_failure(sources, failures[i]);
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
	assert_syntax_failure(sources, (syntax_failure){
		"missing_terminator.pigen",
		"module missing_terminator; int[16] buf value endmodule",
		"signal declaration requires `;`", "endmodule", 1});
	assert_syntax_failure(sources, (syntax_failure){
		"missing_module_typedef_terminator.pigen",
		"module missing_module_typedef_terminator; "
		"typedef int[16] sample_t endmodule",
		"typedef declaration requires `;`", "endmodule", 1});
	assert_syntax_eof_failure(sources,
		"missing_unit_typedef_terminator.pigen",
		"typedef int[16] sample_t",
		"typedef declaration requires `;`");
}

int main(void)
{
	pigen_source_manager sources = {0};

	test_cast_syntax(&sources);
	test_data_first_declarations(&sources);
	test_ordinary_systemverilog_preservation_matrix(&sources);
	test_contextual_leading_transfer_spelling(&sources);
	test_affirmative_declaration_ownership(&sources);
	test_opaque_typedef_shadow_barrier(&sources);
	test_transactional_typedef_ownership(&sources);
	test_transactional_and_clean_break_syntax(&sources);
	test_process_rollback(&sources);
	test_missing_pigen_terminator(&sources);
	pigen_free_sources(&sources);
	puts("PASS: data-first declarations use one transactional syntax topology");
	return 0;
}
