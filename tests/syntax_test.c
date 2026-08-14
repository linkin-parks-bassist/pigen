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

int main(void)
{
	const char text[] =
		"package prefix; localparam module = 1; endpackage\n"
		"typedef logic [31:0] global_word_t;\n"
		"module transports(input logic clk);\n"
		"  typedef bit [3:0] nibble_t;\n"
		"  buf signed [7:0] left, right;\n"
		"  fifo packet_t[DEPTH] queue;\n"
		"  port logic unsigned [15:0] pulse;\n"
		"  always @(posedge clk) begin\n"
		"    right <= left;\n"
		"  end\n"
		"endmodule\n";
	const char invalid[] =
		"module bad; fifo [7:0] queue; endmodule\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "syntax.pigen", text,
		strlen(text));
	pigen_source_id bad_source = pigen_source_add(&sources, "bad.pigen", invalid,
		strlen(invalid));
	pigen_syntax_tree tree = {0};
	pigen_syntax_tree bad_tree = {0};
	pigen_syntax_error error = {0};
	const pigen_syntax_node *root;
	const pigen_syntax_node *module;
	const pigen_syntax_node *node;
	pigen_syntax_id at;
	pigen_syntax_id module_id;
	size_t transports = 0;
	size_t opaques = 0;
	size_t typedefs = 0;

	assert(pigen_parse_syntax(&sources, source, &tree, &error));
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
	assert(span_is(&sources, module->as.module.name, "transports"));

	for (at = module->first_child; at.index != PIGEN_INVALID_ID;
		at = node->next_sibling)
	{
		node = pigen_syntax_get(&tree, at);
		assert(node && node->parent.index == module_id.index);
		if (node->kind == PIGEN_SYNTAX_OPAQUE)
		{
			opaques++;
			continue;
		}
		if (node->kind == PIGEN_SYNTAX_TYPEDEF)
		{
			typedefs++;
			assert(span_is(&sources, node->as.type_definition.name, "nibble_t"));
			continue;
		}
		assert(node->kind == PIGEN_SYNTAX_TRANSPORT);
		transports++;
		if (span_is(&sources, node->as.transport.name, "left"))
		{
			const pigen_syntax_dimension *dimension =
				pigen_syntax_type_dimensions(&tree, &node->as.transport.payload);
			assert(node->as.transport.kind == PIGEN_TRANSPORT_BUF);
			assert(node->as.transport.payload.signedness == PIGEN_SYNTAX_SIGN_SIGNED);
			assert(dimension && span_is(&sources, dimension->left, "7") &&
				span_is(&sources, dimension->right, "0"));
		}
		else if (span_is(&sources, node->as.transport.name, "queue"))
		{
			assert(node->as.transport.kind == PIGEN_TRANSPORT_FIFO);
			assert(node->as.transport.payload.base == PIGEN_SYNTAX_TYPE_NAMED);
			assert(span_is(&sources, node->as.transport.payload.base_name, "packet_t"));
			assert(span_is(&sources, node->as.transport.fifo_depth, "DEPTH"));
		}
		else if (span_is(&sources, node->as.transport.name, "pulse"))
		{
			assert(node->as.transport.kind == PIGEN_TRANSPORT_PORT);
			assert(node->as.transport.payload.base == PIGEN_SYNTAX_TYPE_LOGIC);
			assert(node->as.transport.payload.signedness == PIGEN_SYNTAX_SIGN_UNSIGNED);
		}
	}
	assert(transports == 4);
	assert(typedefs == 2);
	assert(opaques >= 2);

	assert(!pigen_parse_syntax(&sources, bad_source, &bad_tree, &error));
	assert(error.message && strstr(error.message, "depth"));
	pigen_free_syntax_tree(&bad_tree);
	pigen_free_syntax_tree(&tree);
	pigen_free_sources(&sources);
	puts("PASS: modules, typedefs, and transports have structured syntax");
	return 0;
}
