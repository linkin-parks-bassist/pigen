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
	const char *name;
	const char *source;
	const char *message;
	const char *span;
	size_t span_occurrence;
} pipeline_failure;

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

/* A clocked process is the only host for a pipeline; locate the single one. */
static const pigen_syntax_node *fixture_process(const parsed_fixture *fixture)
{
	const pigen_syntax_node *module = fixture_module(fixture);
	pigen_syntax_id child;

	assert(module);
	for (child = module->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS) return node;
		child = node->next_sibling;
	}
	return NULL;
}

static const pigen_syntax_node *find_pipeline(const parsed_fixture *fixture)
{
	const pigen_syntax_node *process = fixture_process(fixture);
	const pigen_syntax_node *statement;
	const pigen_syntax_node *block;

	assert(process);
	assert(process->first_child.index != PIGEN_INVALID_ID);
	block = pigen_syntax_get(&fixture->tree, process->first_child);
	assert(block && block->kind == PIGEN_SYNTAX_PROCEDURAL_BLOCK);
	statement = pigen_syntax_get(&fixture->tree, block->first_child);
	assert(statement);
	return statement->kind == PIGEN_SYNTAX_PIPELINE ? statement : NULL;
}

static void count_pipeline_children(const parsed_fixture *fixture,
	const pigen_syntax_node *pipeline, size_t *field_count,
	size_t *stage_count)
{
	pigen_syntax_id child;

	assert(field_count && stage_count);
	*field_count = 0;
	*stage_count = 0;
	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION) (*field_count)++;
		else if (node->kind == PIGEN_SYNTAX_STAGE) (*stage_count)++;
		child = node->next_sibling;
	}
}

/* The field's declared transfer type is carried by its signal declaration. */
static pigen_transfer_type pipeline_field_transfer(const pigen_syntax_node *field)
{
	assert(field->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION);
	assert(field->as.signal_declaration.has_transfer_type);
	return field->as.signal_declaration.transfer_type.transfer_type;
}

static const pigen_syntax_node *find_pipeline_field(const parsed_fixture *fixture,
	const pigen_syntax_node *pipeline, const char *name)
{
	pigen_syntax_id child;

	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
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
				return node;
			declarator = known->next_sibling;
		}
		child = node->next_sibling;
	}
	return NULL;
}

static const pigen_syntax_node *find_stage(const parsed_fixture *fixture,
	const pigen_syntax_node *pipeline, const char *name, int has_name)
{
	pigen_syntax_id child;

	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_STAGE &&
			node->as.stage.has_name == has_name)
		{
			if (!has_name ||
				token_is(&fixture->preprocessed.expanded,
					node->as.stage.name, name))
				return node;
		}
		child = node->next_sibling;
	}
	return NULL;
}

static void assert_pipeline_failure(pigen_source_manager *sources,
	pipeline_failure failure)
{
	parsed_fixture fixture = {0};
	size_t expected_start = source_occurrence_start(failure.source, failure.span,
		failure.span_occurrence);
	int parsed = parse_fixture(sources, failure.name, failure.source, &fixture);

	if (parsed)
		fprintf(stderr, "expected `%s` to fail pipeline syntax parsing\n",
			failure.name);
	assert(!parsed);
	if (!fixture.error.message ||
		strcmp(fixture.error.message, failure.message) ||
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

/* (1) shape: a well-formed pipeline parses to the structured topology. */
static void test_pipeline_shape(pigen_source_manager *sources)
{
	const char text[] =
		"module shape(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline shaper begin\n"
		"\t\t\tbit [7:0] first;\n"
		"\t\t\tbuf bit [7:0] second;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tbit[7] scratch;\n"
		"\t\t\t\tfirst <= first;\n"
		"\t\t\tend\n"
		"\t\t\tstage named begin\n"
		"\t\t\t\tsecond <= second;\n"
		"\t\t\tend\n"
		"\t\t\tyield second;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *pipeline;
	const pigen_syntax_node *field;
	const pigen_syntax_node *first_stage;
	const pigen_syntax_node *named_stage;
	pigen_syntax_id item;
	size_t field_count, stage_count;
	size_t declaration_items = 0, assignment_items = 0;

	assert(parse_fixture(sources, "shape.pigen", text, &fixture));
	assert(!fixture.error.message);
	pipeline = find_pipeline(&fixture);
	assert(pipeline);
	assert(token_is(&fixture.preprocessed.expanded, pipeline->as.pipeline.name,
		"shaper"));
	count_pipeline_children(&fixture, pipeline, &field_count, &stage_count);
	assert(field_count == 2);
	assert(stage_count == 2);
	assert(expression_is(sources, &fixture.tree, pipeline->as.pipeline.yield,
		"second"));

	field = find_pipeline_field(&fixture, pipeline, "first");
	assert(field);
	assert(pipeline_field_transfer(field) == PIGEN_TRANSFER_TYPE_BUF);
	assert(field->as.signal_declaration.transfer_type.argument.index ==
		PIGEN_INVALID_ID);
	assert(!span_is(sources, field->as.signal_declaration.transfer_type.
		location.source_span, "buf"));

	field = find_pipeline_field(&fixture, pipeline, "second");
	assert(field);
	assert(pipeline_field_transfer(field) == PIGEN_TRANSFER_TYPE_BUF);
	assert(span_is(sources, field->as.signal_declaration.transfer_type.
		location.source_span, "buf"));

	first_stage = find_stage(&fixture, pipeline, NULL, 0);
	assert(first_stage);
	assert(first_stage->as.stage.has_name == 0);
	for (item = first_stage->first_child; item.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture.tree, item);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION) declaration_items++;
		else if (node->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT)
			assignment_items++;
		item = node->next_sibling;
	}
	assert(declaration_items == 1);
	assert(assignment_items == 1);

	named_stage = find_stage(&fixture, pipeline, "named", 1);
	assert(named_stage);
	assert(named_stage->as.stage.has_name == 1);
	assert(token_is(&fixture.preprocessed.expanded, named_stage->as.stage.name,
		"named"));

	free_fixture(&fixture);
}

/* (2) field-buf: every field is buf; other annotations are diagnosed. */
static void test_pipeline_field_buf(pigen_source_manager *sources)
{
	const char omitted_text[] =
		"module omitted(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline omitted_pipe begin\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule";
	const char explicit_text[] =
		"module explicit(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline explicit_pipe begin\n"
		"\t\t\tbuf bit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *pipeline;
	const pigen_syntax_node *field;
	size_t field_count, stage_count;

	assert(parse_fixture(sources, "omitted.pigen", omitted_text, &fixture));
	assert(!fixture.error.message);
	pipeline = find_pipeline(&fixture);
	assert(pipeline);
	count_pipeline_children(&fixture, pipeline, &field_count, &stage_count);
	assert(field_count == 1);
	assert(stage_count == 1);
	field = find_pipeline_field(&fixture, pipeline, "value");
	assert(field);
	assert(pipeline_field_transfer(field) == PIGEN_TRANSFER_TYPE_BUF);
	assert(field->as.signal_declaration.transfer_type.argument.index ==
		PIGEN_INVALID_ID);
	assert(!span_is(sources, field->as.signal_declaration.transfer_type.
		location.source_span, "buf"));
	free_fixture(&fixture);

	assert(parse_fixture(sources, "explicit.pigen", explicit_text, &fixture));
	assert(!fixture.error.message);
	pipeline = find_pipeline(&fixture);
	assert(pipeline);
	field = find_pipeline_field(&fixture, pipeline, "value");
	assert(field);
	assert(pipeline_field_transfer(field) == PIGEN_TRANSFER_TYPE_BUF);
	assert(span_is(sources, field->as.signal_declaration.transfer_type.
		location.source_span, "buf"));
	free_fixture(&fixture);

	assert_pipeline_failure(sources, (pipeline_failure){
		"field_fifo.pigen",
		"module declared(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline declared_pipe begin\n"
		"\t\t\tfifo[8] bit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"pipeline field transfer type must be buf", "fifo", 1});
	assert_pipeline_failure(sources, (pipeline_failure){
		"field_wire.pigen",
		"module annotated(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline annotated_pipe begin\n"
		"\t\t\twire bit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"pipeline field transfer type must be buf", "wire", 1});
	assert_pipeline_failure(sources, (pipeline_failure){
		"field_skid.pigen",
		"module buffered(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline buffered_pipe begin\n"
		"\t\t\tskid bit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"pipeline field transfer type must be buf", "skid", 1});
	assert_pipeline_failure(sources, (pipeline_failure){
		"field_reg.pigen",
		"module declared(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline declared_pipe begin\n"
		"\t\t\treg bit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"pipeline field transfer type must be buf", "reg", 1});
	assert_pipeline_failure(sources, (pipeline_failure){
		"field_logic.pigen",
		"module annotated(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline annotated_pipe begin\n"
		"\t\t\tlogic bit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"pipeline field transfer type must be buf", "logic", 2});
	assert_pipeline_failure(sources, (pipeline_failure){
		"field_port.pigen",
		"module buffered(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline buffered_pipe begin\n"
		"\t\t\tport bit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"pipeline field transfer type must be buf", "port", 1});
}

/* (3) scope: stage-private scopes with the ordered lookup and diagnostics. */
static void test_pipeline_scope(pigen_source_manager *sources)
{
	const char nested_text[] =
		"module nested(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline nested_pipe begin\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tbit[7] scratch;\n"
		"\t\t\t\tvalue <= scratch;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *pipeline;
	const pigen_syntax_node *stage;
	const pigen_syntax_node *field;
	pigen_syntax_id item;
	size_t field_count, stage_count;
	size_t declaration_items = 0;

	assert(parse_fixture(sources, "nested.pigen", nested_text, &fixture));
	assert(!fixture.error.message);
	pipeline = find_pipeline(&fixture);
	assert(pipeline);
	count_pipeline_children(&fixture, pipeline, &field_count, &stage_count);
	assert(field_count == 1);
	assert(stage_count == 1);
	field = find_pipeline_field(&fixture, pipeline, "value");
	assert(field);
	assert(pipeline_field_transfer(field) == PIGEN_TRANSFER_TYPE_BUF);
	stage = find_stage(&fixture, pipeline, NULL, 0);
	assert(stage);
	for (item = stage->first_child; item.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture.tree, item);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION) declaration_items++;
		item = node->next_sibling;
	}
	assert(declaration_items == 1);
	free_fixture(&fixture);

	assert_pipeline_failure(sources, (pipeline_failure){
		"scope_duplicate_field.pigen",
		"module scope_duplicate_field(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline dup_field_pipe begin\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"duplicate pipeline field name", "value", 2});
	assert_pipeline_failure(sources, (pipeline_failure){
		"scope_shadow_field.pigen",
		"module scope_shadow_field(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline shadow_field_pipe begin\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tbit[7] value;\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"stage-local name shadows a pipeline field", "value", 2});
	assert_pipeline_failure(sources, (pipeline_failure){
		"scope_duplicate_stage.pigen",
		"module scope_duplicate_stage(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline dup_stage_pipe begin\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tbit[7] local;\n"
		"\t\t\t\tbit[7] local;\n"
		"\t\t\t\tvalue <= local;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"duplicate stage-local name", "local", 2});
}

/* (4) yield: exactly one final yield, misplaced/missing/duplicate diagnosed. */
static void test_pipeline_yield(pigen_source_manager *sources)
{
	const char final_text[] =
		"module yield_final(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline yield_final_pipe begin\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *pipeline;
	size_t field_count, stage_count;

	assert(parse_fixture(sources, "yield_final.pigen", final_text, &fixture));
	assert(!fixture.error.message);
	pipeline = find_pipeline(&fixture);
	assert(pipeline);
	count_pipeline_children(&fixture, pipeline, &field_count, &stage_count);
	assert(field_count == 1);
	assert(stage_count == 1);
	assert(pipeline->as.pipeline.yield.index != PIGEN_INVALID_ID);
	assert(expression_is(sources, &fixture.tree, pipeline->as.pipeline.yield,
		"value"));
	free_fixture(&fixture);

	assert_pipeline_failure(sources, (pipeline_failure){
		"yield_misplaced.pigen",
		"module misplaced(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline misplaced_pipe begin\n"
		"\t\t\tyield value;\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"pipeline yield must follow the stages", "yield", 1});
	assert_pipeline_failure(sources, (pipeline_failure){
		"yield_missing.pigen",
		"module yield_missing(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline missing_pipe begin\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"pipeline requires a final yield", "endpipeline", 1});
	assert_pipeline_failure(sources, (pipeline_failure){
		"yield_duplicate.pigen",
		"module duplicate(input logic clk);\n"
		"\talways @(posedge clk) begin\n"
		"\t\tpipeline duplicate_pipe begin\n"
		"\t\t\tbit [7:0] value;\n"
		"\t\t\tstage begin\n"
		"\t\t\t\tvalue <= value;\n"
		"\t\t\tend\n"
		"\t\t\tyield value;\n"
		"\t\t\tyield value;\n"
		"\t\tendpipeline\n"
		"\tend\n"
		"endmodule",
		"pipeline permits exactly one yield", "yield", 2});
}

int main(int argc, char **argv)
{
	pigen_source_manager sources = {0};

	if (argc > 1 && strcmp(argv[1], "shape") == 0)
		test_pipeline_shape(&sources);
	else if (argc > 1 && strcmp(argv[1], "field-buf") == 0)
		test_pipeline_field_buf(&sources);
	else if (argc > 1 && strcmp(argv[1], "scope") == 0)
		test_pipeline_scope(&sources);
	else if (argc > 1 && strcmp(argv[1], "yield") == 0)
		test_pipeline_yield(&sources);
	else
	{
		fprintf(stderr, "usage: pipeline-identity-test "
			"{shape|field-buf|scope|yield}\n");
		return 2;
	}
	pigen_free_sources(&sources);
	printf("PASS: pipeline-identity-test %s\n", argv[1]);
	return 0;
}
