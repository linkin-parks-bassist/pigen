#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/syntax.h"

static int span_is(const pigen_source_manager *sources, pigen_source_span span,
	const char *expected)
{
	size_t length;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static int span_contains(const pigen_source_manager *sources,
	pigen_source_span span, const char *needle)
{
	size_t length;
	size_t needle_length = strlen(needle);
	const char *text = pigen_source_span_text(sources, span, &length);
	size_t i;

	if (!text || needle_length > length) return 0;
	for (i = 0; i + needle_length <= length; i++)
		if (!memcmp(text + i, needle, needle_length)) return 1;
	return 0;
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

int main(void)
{
	const char text[] =
		"package prefix; localparam module = 1; endpackage\n"
		"typedef logic [31:0] global_word_t;\n"
		"module transports #(parameter WIDTH = 8, DEPTH = WIDTH + 1) (input logic clk);\n"
		"  localparam PULSE_W = WIDTH * 2;\n"
		"  typedef bit [3:0] nibble_t;\n"
		"  buf signed [WIDTH-1:0] left, right;\n"
		"  fifo packet_t[DEPTH] queue;\n"
		"  port logic unsigned [15:0] pulse;\n"
		"  logic [7:0] memory [0:3];\n"
		"  always @(posedge clk) begin\n"
		"    if (clk)\n"
		"      if (clk)\n"
		"        right <= left;\n"
		"      else\n"
		"        left <= right;\n"
		"  end\n"
		"endmodule\n"
		"module ordinary #(parameter int X = 1);\n"
		"  localparam logic [3:0] Y = 4;\n"
		"  logic [X-1:0] passthrough;\n"
		"endmodule\n";
	const char invalid[] =
		"module bad; fifo [7:0] queue; endmodule\n";
	const char rollback[] =
		"module rollback(input logic clk);\n"
		"  buf bit left, right;\n"
		"  always @(posedge clk) begin\n"
		"    right <= left;\n"
		"    $display(\"opaque\");\n"
		"  end\n"
		"endmodule\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "syntax.pigen", text,
		strlen(text));
	pigen_source_id bad_source = pigen_source_add(&sources, "bad.pigen", invalid,
		strlen(invalid));
	pigen_source_id rollback_source = pigen_source_add(&sources,
		"rollback.pigen", rollback, strlen(rollback));
	pigen_syntax_tree tree = {0};
	pigen_syntax_tree bad_tree = {0};
	pigen_syntax_tree rollback_tree = {0};
	pigen_preprocess_result preprocessed = {0};
	pigen_preprocess_result bad_preprocessed = {0};
	pigen_preprocess_result rollback_preprocessed = {0};
	pigen_preprocess_error preprocess_error = {0};
	pigen_syntax_error error = {0};
	const pigen_syntax_node *root;
	const pigen_syntax_node *module;
	const pigen_syntax_node *node;
	pigen_syntax_id at;
	pigen_syntax_id module_id;
	size_t transports = 0;
	size_t opaques = 0;
	size_t typedefs = 0;
	size_t parameters = 0;
	size_t values = 0;
	size_t clocked_processes = 0;
	size_t assignments = 0;
	int unpacked_is_opaque = 0;

	assert(pigen_preprocess(&sources, source, NULL, &preprocessed,
		&preprocess_error));
	assert(pigen_parse_syntax(&preprocessed.expanded, &tree, &error));
	root = pigen_syntax_get(&tree, (pigen_syntax_id){0});
	assert(root && root->kind == PIGEN_SYNTAX_COMPILATION_UNIT);
	at = root->first_child;
	assert(pigen_syntax_get(&tree, at)->kind == PIGEN_SYNTAX_OPAQUE);
	while (pigen_syntax_get(&tree, at)->kind != PIGEN_SYNTAX_MODULE)
	{
		if (pigen_syntax_get(&tree, at)->kind == PIGEN_SYNTAX_TYPEDEF)
			typedefs++;
		at = pigen_syntax_get(&tree, at)->next_sibling;
	}
	module_id = at;
	module = pigen_syntax_get(&tree, at);
	assert(module && module->kind == PIGEN_SYNTAX_MODULE);
	assert(token_is(&preprocessed.expanded, module->as.module.name, "transports"));

	for (at = module->first_child; at.index != PIGEN_INVALID_ID;
		at = node->next_sibling)
	{
		node = pigen_syntax_get(&tree, at);
		assert(node && node->parent.index == module_id.index);
		if (node->kind == PIGEN_SYNTAX_OPAQUE)
		{
			opaques++;
			if (span_contains(&sources, node->location.source_span, "memory"))
				unpacked_is_opaque = 1;
			continue;
		}
		if (node->kind == PIGEN_SYNTAX_TYPEDEF)
		{
			typedefs++;
			assert(token_is(&preprocessed.expanded, node->as.type_definition.name,
				"nibble_t"));
			continue;
		}
		if (node->kind == PIGEN_SYNTAX_PARAMETER)
		{
			parameters++;
			if (token_is(&preprocessed.expanded, node->as.parameter.name,
				"PULSE_W"))
			{
				assert(node->as.parameter.is_local);
				assert(expression_is(&sources, &tree,
					node->as.parameter.value, "WIDTH * 2"));
			}
			else
				assert(!node->as.parameter.is_local);
			continue;
		}
		if (node->kind == PIGEN_SYNTAX_VALUE_DECLARATION)
		{
			const pigen_syntax_node *declarator = pigen_syntax_get(&tree,
				node->first_child);
			assert(declarator &&
				declarator->kind == PIGEN_SYNTAX_VALUE_DECLARATOR);
			assert(token_is(&preprocessed.expanded,
				declarator->as.value_declarator.name, "clk"));
			assert(node->as.value_declaration.direction == PIGEN_DIRECTION_INPUT);
			assert(node->as.value_declaration.storage == PIGEN_VALUE_NET);
			assert(node->as.value_declaration.type.base == PIGEN_SYNTAX_TYPE_LOGIC);
			values++;
			continue;
		}
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS)
		{
			const pigen_syntax_node *block = pigen_syntax_get(&tree,
				node->first_child);
			const pigen_syntax_node *outer_if;
			const pigen_syntax_node *inner_if;
			const pigen_syntax_node *assignment;
			assert(node->as.clocked_process.edge == PIGEN_EDGE_POSEDGE);
			assert(expression_is(&sources, &tree,
				node->as.clocked_process.clock, "clk"));
			assert(block && block->kind == PIGEN_SYNTAX_PROCEDURAL_BLOCK &&
				block->parent.index == at.index);
			outer_if = pigen_syntax_get(&tree, block->first_child);
			assert(outer_if && outer_if->kind == PIGEN_SYNTAX_IF_STATEMENT &&
				!outer_if->as.if_statement.has_else);
			inner_if = pigen_syntax_get(&tree, outer_if->first_child);
			assert(inner_if && inner_if->kind == PIGEN_SYNTAX_IF_STATEMENT &&
				inner_if->as.if_statement.has_else);
			assignment = pigen_syntax_get(&tree, inner_if->first_child);
			assert(assignment &&
				assignment->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT &&
				assignment->parent.index == outer_if->first_child.index);
			assert(expression_is(&sources, &tree,
				assignment->as.nonblocking_assignment.destination, "right"));
			assert(expression_is(&sources, &tree,
				assignment->as.nonblocking_assignment.value, "left"));
			assignment = pigen_syntax_get(&tree, assignment->next_sibling);
			assert(assignment &&
				assignment->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			assert(expression_is(&sources, &tree,
				assignment->as.nonblocking_assignment.destination, "left"));
			assert(expression_is(&sources, &tree,
				assignment->as.nonblocking_assignment.value, "right"));
			assert(assignment->next_sibling.index == PIGEN_INVALID_ID);
			clocked_processes++;
			assignments += 2;
			continue;
		}
		assert(node->kind == PIGEN_SYNTAX_TRANSPORT_DECLARATION);
		for (pigen_syntax_id declarator_id = node->first_child;
			declarator_id.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *declarator =
				pigen_syntax_get(&tree, declarator_id);
			assert(declarator &&
				declarator->kind == PIGEN_SYNTAX_TRANSPORT_DECLARATOR);
			assert(declarator->parent.index == at.index);
			transports++;
			if (token_is(&preprocessed.expanded,
				declarator->as.transport_declarator.name, "left"))
			{
				const pigen_syntax_dimension *dimension =
					pigen_syntax_type_dimensions(&tree,
						&node->as.transport_declaration.payload);
				assert(node->as.transport_declaration.kind == PIGEN_TRANSPORT_BUF);
				assert(node->as.transport_declaration.payload.signedness ==
					PIGEN_SYNTAX_SIGN_SIGNED);
				assert(dimension && expression_is(&sources, &tree,
					dimension->left, "WIDTH-1") && expression_is(&sources, &tree,
						dimension->right, "0"));
			}
			else if (token_is(&preprocessed.expanded,
				declarator->as.transport_declarator.name, "queue"))
			{
				assert(node->as.transport_declaration.kind == PIGEN_TRANSPORT_FIFO);
				assert(node->as.transport_declaration.payload.base ==
					PIGEN_SYNTAX_TYPE_NAMED);
				assert(token_is(&preprocessed.expanded,
					node->as.transport_declaration.payload.base_name, "packet_t"));
				assert(expression_is(&sources, &tree,
					node->as.transport_declaration.fifo_depth, "DEPTH"));
			}
			else if (token_is(&preprocessed.expanded,
				declarator->as.transport_declarator.name, "pulse"))
			{
				assert(node->as.transport_declaration.kind == PIGEN_TRANSPORT_PORT);
				assert(node->as.transport_declaration.payload.base ==
					PIGEN_SYNTAX_TYPE_LOGIC);
				assert(node->as.transport_declaration.payload.signedness ==
					PIGEN_SYNTAX_SIGN_UNSIGNED);
			}
			declarator_id = declarator->next_sibling;
		}
	}
	assert(transports == 4);
	assert(typedefs == 2);
	assert(parameters == 3);
	assert(values == 1);
	assert(clocked_processes == 1);
	assert(assignments == 2);
	assert(unpacked_is_opaque);
	assert(opaques >= 2);

	assert(pigen_preprocess(&sources, bad_source, NULL, &bad_preprocessed,
		&preprocess_error));
	assert(!pigen_parse_syntax(&bad_preprocessed.expanded, &bad_tree, &error));
	assert(error.message && strstr(error.message, "depth"));

	assert(pigen_preprocess(&sources, rollback_source, NULL,
		&rollback_preprocessed, &preprocess_error));
	assert(pigen_parse_syntax(&rollback_preprocessed.expanded, &rollback_tree,
		&error));
	root = pigen_syntax_get(&rollback_tree, (pigen_syntax_id){0});
	assert(root && root->kind == PIGEN_SYNTAX_COMPILATION_UNIT);
	module = pigen_syntax_get(&rollback_tree, root->first_child);
	assert(module && module->kind == PIGEN_SYNTAX_MODULE);
	clocked_processes = 0;
	for (at = module->first_child; at.index != PIGEN_INVALID_ID;
		at = node->next_sibling)
	{
		node = pigen_syntax_get(&rollback_tree, at);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS) clocked_processes++;
	}
	assert(!clocked_processes);
	assert(rollback_tree.expressions.node_count == 0);
	pigen_free_syntax_tree(&bad_tree);
	pigen_free_syntax_tree(&rollback_tree);
	pigen_free_syntax_tree(&tree);
	pigen_free_preprocess_result(&bad_preprocessed);
	pigen_free_preprocess_result(&rollback_preprocessed);
	pigen_free_preprocess_result(&preprocessed);
	pigen_free_sources(&sources);
	puts("PASS: declarations and recursive clocked controls have structured syntax");
	return 0;
}
