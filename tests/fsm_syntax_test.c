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

static int span_is(const pigen_source_manager *sources, pigen_source_span span,
	const char *expected)
{
	size_t length;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

/* Pins a node's original span to exact byte coordinates: a dropped or zeroed
 * span (start == end) or a wrong source cannot match. */
static int span_is_range(const pigen_source_manager *sources, pigen_source_span span,
	pigen_source_id source_id, size_t start, size_t end)
{
	size_t length;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && span.source.index == source_id.index &&
		span.start == start && span.end == end && end > start;
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
	const pigen_expanded_token *known = pigen_expanded_token_get(source, token);
	const char *text;
	size_t length;
	if (!known) return 0;
	text = pigen_expanded_token_text(source, known, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static int expression_span_is(const pigen_source_manager *sources,
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

static const pigen_syntax_node *find_fsm(const parsed_fixture *fixture,
	const pigen_syntax_node *module)
{
	pigen_syntax_id child;

	for (child = module->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_FSM) return node;
		child = node->next_sibling;
	}
	return NULL;
}

static void count_kinds(const pigen_syntax_tree *tree, pigen_syntax_id node,
	size_t *goto_count, size_t *assignment_count, size_t *if_count)
{
	pigen_syntax_id child;
	const pigen_syntax_node *known;

	if (node.index == PIGEN_INVALID_ID) return;
	known = pigen_syntax_get(tree, node);
	assert(known);
	switch (known->kind)
	{
	case PIGEN_SYNTAX_GOTO:
		(*goto_count)++;
		break;
	case PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT:
		(*assignment_count)++;
		break;
	case PIGEN_SYNTAX_IF_STATEMENT:
		(*if_count)++;
		break;
	default:
		break;
	}
	for (child = known->first_child; child.index != PIGEN_INVALID_ID; )
	{
		count_kinds(tree, child, goto_count, assignment_count, if_count);
		child = pigen_syntax_get(tree, child)->next_sibling;
	}
}

/* Returns the first PIGEN_SYNTAX_GOTO node under node, or NULL. */
static const pigen_syntax_node *find_goto(const pigen_syntax_tree *tree,
	pigen_syntax_id node)
{
	const pigen_syntax_node *known;
	pigen_syntax_id child;

	if (node.index == PIGEN_INVALID_ID) return NULL;
	known = pigen_syntax_get(tree, node);
	assert(known);
	if (known->kind == PIGEN_SYNTAX_GOTO) return known;
	for (child = known->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *found = find_goto(tree, child);
		if (found) return found;
		child = pigen_syntax_get(tree, child)->next_sibling;
	}
	return NULL;
}

static int read_file(const char *path, char *buffer, size_t capacity)
{
	FILE *file = fopen(path, "rb");
	size_t length;

	if (!file) return 0;
	length = fread(buffer, 1, capacity - 1, file);
	buffer[length] = '\0';
	fclose(file);
	return 1;
}

static void test_legal_fsm(pigen_source_manager *sources)
{
	const char text[] =
		"module sender(input logic clk, input logic reset, input logic start);\n"
		"\tlogic [7:0] source;\n"
		"\tlogic [7:0] destination;\n"
		"\tfsm sender @(posedge clk) reset (reset) initial idle\n"
		"\tbegin\n"
		"\t\tstate idle:\n"
		"\t\tbegin\n"
		"\t\t\tif (start)\n"
		"\t\t\t\tgoto send;\n"
		"\t\tend\n"
		"\t\tstate send:\n"
		"\t\tbegin\n"
		"\t\t\tdestination <= source;\n"
		"\t\t\tif (start)\n"
		"\t\t\t\tgoto idle;\n"
		"\t\tend\n"
		"\tend\n"
		"endmodule";
	parsed_fixture fixture = {0};
	const pigen_syntax_node *module;
	const pigen_syntax_node *fsm;
	const pigen_syntax_node *state;
	const pigen_syntax_node *body;
	pigen_syntax_id child;
	size_t state_count = 0;
	size_t goto_count = 0;
	size_t assignment_count = 0;
	size_t if_count = 0;
	const pigen_syntax_node *goto_node;

	assert(parse_fixture(sources, "legal_fsm.pigen", text, &fixture));
	assert(!fixture.error.message);
	module = fixture_module(&fixture);
	assert(module);
	fsm = find_fsm(&fixture, module);
	assert(fsm);
	assert(token_is(&fixture.preprocessed.expanded, fsm->as.fsm.name, "sender"));
	assert(token_is(&fixture.preprocessed.expanded, fsm->as.fsm.initial, "idle"));
	assert(expression_span_is(sources, &fixture.tree, fsm->as.fsm.clock, "clk"));
	assert(expression_span_is(sources, &fixture.tree, fsm->as.fsm.reset,
		"reset"));
	/* The FSM node carries the original span of the whole construct, from the
	 * `fsm` keyword (byte 118) through the outer `end` (byte 322, exclusive). */
	assert(span_is_range(sources, fsm->location.source_span, fixture.source,
		118, 322));

	for (child = fsm->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture.tree, child);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_FSM_STATE) state_count++;
		child = node->next_sibling;
	}
	assert(state_count == 2);

	state = pigen_syntax_get(&fixture.tree, fsm->first_child);
	assert(state && state->kind == PIGEN_SYNTAX_FSM_STATE);
	assert(token_is(&fixture.preprocessed.expanded, state->as.fsm_state.name,
		"idle"));
	/* Each state node spans `state <name>:` through its body's `end`. */
	assert(span_is_range(sources, state->location.source_span, fixture.source,
		180, 234));
	state = pigen_syntax_get(&fixture.tree, state->next_sibling);
	assert(state && state->kind == PIGEN_SYNTAX_FSM_STATE);
	assert(token_is(&fixture.preprocessed.expanded, state->as.fsm_state.name,
		"send"));
	assert(span_is_range(sources, state->location.source_span, fixture.source,
		237, 317));

	for (child = fsm->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture.tree, child);
		assert(node && node->kind == PIGEN_SYNTAX_FSM_STATE);
		body = pigen_syntax_get(&fixture.tree, node->first_child);
		assert(body && body->kind == PIGEN_SYNTAX_PROCEDURAL_BLOCK);
		count_kinds(&fixture.tree, child, &goto_count, &assignment_count,
			&if_count);
		child = node->next_sibling;
	}
	assert(goto_count == 2);
	assert(assignment_count == 1);
	assert(if_count == 2);
	/* Each goto node spans the full statement including the semicolon. The
	 * first state (idle) issues `goto send;`, the second (send) `goto idle;`. */
	state = pigen_syntax_get(&fixture.tree, fsm->first_child);
	assert(state && state->kind == PIGEN_SYNTAX_FSM_STATE);
	goto_node = find_goto(&fixture.tree, state->first_child);
	assert(goto_node);
	assert(span_is(sources, goto_node->location.source_span, "goto send;"));
	state = pigen_syntax_get(&fixture.tree, state->next_sibling);
	assert(state && state->kind == PIGEN_SYNTAX_FSM_STATE);
	goto_node = find_goto(&fixture.tree, state->first_child);
	assert(goto_node);
	assert(span_is(sources, goto_node->location.source_span, "goto idle;"));
	free_fixture(&fixture);
}

typedef struct {
	const char *path;
	const char *message;
	const char *span;
	size_t span_occurrence;
} syntax_failure;

static void assert_syntax_failure(pigen_source_manager *sources,
	syntax_failure failure)
{
	char buffer[16384];
	parsed_fixture fixture = {0};

	assert(read_file(failure.path, buffer, sizeof(buffer)));
	if (parse_fixture(sources, failure.path, buffer, &fixture))
		fprintf(stderr, "expected `%s` to fail syntax parsing\n", failure.path);
	if (!fixture.error.message || strcmp(fixture.error.message,
		failure.message) ||
		!span_is_source_occurrence(sources, fixture.error.span,
			fixture.source, buffer, failure.span, failure.span_occurrence))
	{
		size_t start = source_occurrence_start(buffer, failure.span,
			failure.span_occurrence);
		fprintf(stderr,
			"expected `%s` at `%s` occurrence %zu, byte range %zu:%zu; "
			"got `%s` at byte range %zu:%zu\n",
			failure.message, failure.span, failure.span_occurrence, start,
			start + strlen(failure.span),
			fixture.error.message ? fixture.error.message : "(none)",
			fixture.error.span.start, fixture.error.span.end);
	}
	assert(fixture.error.message &&
		!strcmp(fixture.error.message, failure.message));
	assert(span_is_source_occurrence(sources, fixture.error.span,
		fixture.source, buffer, failure.span, failure.span_occurrence));
	free_fixture(&fixture);
}

static void assert_inline_syntax_failure(pigen_source_manager *sources,
	const char *name, const char *source, const char *message,
	const char *span, size_t span_occurrence)
{
	parsed_fixture fixture = {0};
	size_t start;

	if (parse_fixture(sources, name, source, &fixture))
		fprintf(stderr, "expected `%s` to fail syntax parsing\n", name);
	if (!fixture.error.message || strcmp(fixture.error.message, message) ||
		!span_is_source_occurrence(sources, fixture.error.span,
			fixture.source, source, span, span_occurrence))
	{
		start = source_occurrence_start(source, span, span_occurrence);
		fprintf(stderr,
			"expected `%s` at `%s` occurrence %zu, byte range %zu:%zu; "
			"got `%s` at byte range %zu:%zu\n",
			message, span, span_occurrence, start,
			start + strlen(span),
			fixture.error.message ? fixture.error.message : "(none)",
			fixture.error.span.start, fixture.error.span.end);
	}
	assert(fixture.error.message && !strcmp(fixture.error.message, message));
	assert(span_is_source_occurrence(sources, fixture.error.span,
		fixture.source, source, span, span_occurrence));
	free_fixture(&fixture);
}

int main(void)
{
	pigen_source_manager sources = {0};

	test_legal_fsm(&sources);
	assert_syntax_failure(&sources, (syntax_failure){
		"tests/fsm_bad_event.pigen",
		"fsm event control must be one posedge", "or", 1});
	assert_syntax_failure(&sources, (syntax_failure){
		"tests/fsm_bad_goto.pigen",
		"goto references an undeclared state", "missing", 1});
	assert_syntax_failure(&sources, (syntax_failure){
		"tests/fsm_bad_initial.pigen",
		"fsm initial state is not declared", "missing", 1});
	assert_syntax_failure(&sources, (syntax_failure){
		"tests/fsm_duplicate_state.pigen",
		"fsm state declared more than once", "idle", 3});
	assert_inline_syntax_failure(&sources, "missing_reset.pigen",
		"module missing_reset(input logic clk, input logic reset);\n"
		"\tfsm sender @(posedge clk) initial idle\n"
		"\tbegin\n"
		"\t\tstate idle:\n"
		"\t\tbegin\n"
		"\t\tend\n"
		"\tend\n"
		"endmodule",
		"fsm requires `reset (name)`", "initial", 1);
	assert_inline_syntax_failure(&sources, "missing_initial.pigen",
		"module missing_initial(input logic clk, input logic reset);\n"
		"\tfsm sender @(posedge clk) reset (reset)\n"
		"\tbegin\n"
		"\t\tstate idle:\n"
		"\t\tbegin\n"
		"\t\tend\n"
		"\tend\n"
		"endmodule",
		"fsm requires an initial state", "begin", 1);
	pigen_free_sources(&sources);
	puts("PASS: fsm syntax parses to structured nodes");
	return 0;
}
