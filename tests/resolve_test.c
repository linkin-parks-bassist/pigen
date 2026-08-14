#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/resolve.h"

static int span_is(const pigen_source_manager *sources, pigen_source_span span,
	const char *expected)
{
	size_t length;
	const char *text = pigen_source_span_text(sources, span, &length);
	return text && length == strlen(expected) && !memcmp(text, expected, length);
}

static const pigen_semantic_transport *find_transport(
	const pigen_source_manager *sources, const pigen_semantic_model *model,
	const char *name)
{
	size_t i;
	for (i = 0; i < model->transport_count; i++)
	{
		const pigen_symbol *symbol = pigen_symbol_get(model,
			model->transports[i].symbol);
		if (span_is(sources, symbol->name, name)) return &model->transports[i];
	}
	return NULL;
}

int main(void)
{
	const char text[] =
		"typedef logic [31:0] word_t;\n"
		"module first;\n"
		"  typedef logic unsigned [7:0] byte_t;\n"
		"  buf byte_t left, right;\n"
		"  fifo word_t[4] queue;\n"
		"endmodule\n"
		"module second;\n"
		"  port bit [0:0] pulse;\n"
		"endmodule\n";
	const char duplicate[] =
		"module same; endmodule module same; endmodule\n";
	const char unknown[] =
		"module unknown; buf missing_t value; endmodule\n";
	const char inout[] =
		"module bidirectional; inout buf [7:0] value; endmodule\n";
	pigen_source_manager sources = {0};
	pigen_source_id source = pigen_source_add(&sources, "resolve.pigen", text,
		strlen(text));
	pigen_source_id duplicate_source = pigen_source_add(&sources, "duplicate.pigen",
		duplicate, strlen(duplicate));
	pigen_source_id unknown_source = pigen_source_add(&sources, "unknown.pigen",
		unknown, strlen(unknown));
	pigen_source_id inout_source = pigen_source_add(&sources, "inout.pigen", inout,
		strlen(inout));
	pigen_syntax_tree syntax = {0};
	pigen_syntax_tree duplicate_syntax = {0};
	pigen_syntax_tree unknown_syntax = {0};
	pigen_syntax_tree inout_syntax = {0};
	pigen_syntax_error syntax_error = {0};
	pigen_semantic_model model;
	pigen_semantic_model duplicate_model;
	pigen_semantic_model unknown_model;
	pigen_semantic_model inout_model;
	pigen_resolve_error error = {0};
	const pigen_semantic_transport *left;
	const pigen_semantic_transport *right;
	const pigen_semantic_transport *queue;
	const pigen_semantic_type *queue_type;
	const pigen_packed_dimension *queue_dimension;
	const pigen_semantic_expr *bound;

	assert(pigen_parse_syntax(&sources, source, &syntax, &syntax_error));
	assert(pigen_resolve_declarations(&sources, &syntax, &model, &error));
	assert(model.compilation_scope.index != PIGEN_INVALID_ID);
	assert(model.module_count == 2);
	assert(model.transport_count == 4);
	assert(pigen_module_get(&model, (pigen_module_id){0})->scope.index !=
		PIGEN_INVALID_ID);

	left = find_transport(&sources, &model, "left");
	right = find_transport(&sources, &model, "right");
	queue = find_transport(&sources, &model, "queue");
	assert(left && right && queue);
	assert(left->payload_type.index == right->payload_type.index);
	assert(left->kind == PIGEN_SEMANTIC_BUF);
	assert(queue->kind == PIGEN_SEMANTIC_FIFO);
	assert(queue->fifo_depth.index != PIGEN_INVALID_ID);
	assert(pigen_expr_get(&model, queue->fifo_depth)->integer == 4);
	queue_type = pigen_type_get(&model, queue->payload_type);
	assert(queue_type && queue_type->kind == PIGEN_TYPE_NAMED);
	queue_type = pigen_type_get(&model,
		pigen_symbol_get(&model, queue_type->named_symbol)->type);
	assert(queue_type && queue_type->kind == PIGEN_TYPE_LOGIC);
	queue_dimension = pigen_type_dimensions(&model,
		pigen_symbol_get(&model,
			pigen_type_get(&model, queue->payload_type)->named_symbol)->type);
	assert(queue_dimension);
	bound = pigen_expr_get(&model, queue_dimension->left);
	assert(bound && bound->integer == 31);

	assert(pigen_parse_syntax(&sources, duplicate_source, &duplicate_syntax,
		&syntax_error));
	assert(!pigen_resolve_declarations(&sources, &duplicate_syntax,
		&duplicate_model, &error));
	assert(error.message && strstr(error.message, "duplicate module"));
	assert(span_is(&sources, error.span, "same"));

	assert(pigen_parse_syntax(&sources, unknown_source, &unknown_syntax,
		&syntax_error));
	assert(!pigen_resolve_declarations(&sources, &unknown_syntax,
		&unknown_model, &error));
	assert(error.message && strstr(error.message, "unknown type"));
	assert(span_is(&sources, error.span, "missing_t"));
	assert(pigen_parse_syntax(&sources, inout_source, &inout_syntax, &syntax_error));
	assert(!pigen_resolve_declarations(&sources, &inout_syntax, &inout_model, &error));
	assert(error.message && strstr(error.message, "not inout"));

	pigen_free_semantic_model(&inout_model);
	pigen_free_semantic_model(&unknown_model);
	pigen_free_semantic_model(&duplicate_model);
	pigen_free_semantic_model(&model);
	pigen_free_syntax_tree(&inout_syntax);
	pigen_free_syntax_tree(&unknown_syntax);
	pigen_free_syntax_tree(&duplicate_syntax);
	pigen_free_syntax_tree(&syntax);
	pigen_free_sources(&sources);
	puts("PASS: typedef and transport syntax resolves by scope and identity");
	return 0;
}
