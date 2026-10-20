/* Pipeline identifier output-identity test contract.
 *
 * Test-contract stage of step (a) of the pipeline frontier: the discriminating
 * manifest that pins the pipeline identifier as a module-scope buf output
 * packet identity on the structured path. It tests the skeleton's owner seam
 * (include/pigen/pipeline_identity.h, pigen_pipeline_identifier_establish /
 * pigen_pipeline_identifier_of) against the behavior contract the spec and the
 * pipeline leaves require (spec: what/is/the/spec.md, what/does/pipeline/yield/mean.md,
 * how/are/scopes/and/symbol/bindings/represented.md, what/is/a/pipeline.md):
 *
 *   - A module containing one `pipeline <identifier> begin <declaration>*
 *     <stage>+ yield <expr>; endpipeline` inside a clocked process, once lowered
 *     through the structured path, establishes the pipeline identifier as a
 *     module-scope signal: transfer_type = buf, direction = PIGEN_SEMANTIC_OUTPUT,
 *     data_type = the outgoing packet aggregate (the field(s) the final yield
 *     reads), bound to a stable module-scope symbol, with a source_span anchored
 *     at the pipeline identifier's original span.
 *   - The signal is a module-scope identity: recoverable from the module's
 *     signal set / symbol and NOT a stage-private or pipeline-private identity.
 *   - Every pipeline field is a buf (omission canonical, explicit buf
 *     equivalent); the output packet's transfer type is the same buf the fields
 *     carry (no second dialect).
 *
 * Witness construction: each section parses a legal pipeline through the
 * structured syntax path (pigen_parse_syntax) to obtain the pipeline node, its
 * identifier token and its yield expression, and builds the surrounding
 * resolved module through the owner APIs (pigen_semantic_init,
 * pigen_module_add, pigen_scope_add, pigen_symbol_declare, pigen_signal_add,
 * pigen_clock_domain_intern, pigen_process_add) - the same module-scope
 * skeleton the resolver builds for a clocked process, since the structured
 * lowerer does not yet carry a pipeline statement through
 * pigen_resolve_semantics (that is a later pipeline step). The section then
 * drives the skeleton's establish entry on that model and asserts the
 * established identity and the signal it creates, read back exclusively through
 * the owner accessors (pigen_pipeline_identifier_of, pigen_signal_get,
 * pigen_symbol_get, pigen_symbol_signal, pigen_module_get, pigen_scope_get and
 * the transfer-type / data-type owner algebra). Every witness is located
 * through the parse (the pipeline node, the identifier token origin, the field
 * node spans, the clocked-process clock span); the expected outgoing width is
 * read from each field's bounded declaration span (the [ .. ] range within that
 * span), not located by a source-text search. No prototype reuse, no editing of
 * the prototype's pipeline-test / pipeline-scope-test / pipeline-syntax-test
 * witnesses.
 *
 * The manifest is a four-section discriminating manifest wired as one
 * `make pipeline-output-test T=<section>` entry per section in
 * tests/contracts.json; the Makefile target compiles the test with the exact
 * CFLAGS (-std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude) over the
 * full parse + lower + semantic source list. Every section compiles and runs
 * today: it fails at its REQUIRE(pigen_pipeline_identifier_establish(...) == 0)
 * against the current skeleton stub (which returns -1), and passes once the
 * implementation lands. Each section independently discriminates the identity
 * contract: a builder that (i) drops the output direction, (ii) mis-types the
 * outgoing packet, or (iii) introduces the identifier into a private scope
 * turns exactly its section red, and a permissive builder that accepts
 * anything fails the sections.
 */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/data_type.h"
#include "pigen/expression.h"
#include "pigen/pipeline_identity.h"
#include "pigen/preprocess.h"
#include "pigen/semantic.h"
#include "pigen/source.h"
#include "pigen/syntax.h"
#include "pigen/transfer_type.h"

#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)

/* A span over the parsed source that must hold exactly WORD (owner text check
 * against the span, not a source-text search over the output). */
static int span_is(const pigen_source_manager *srcs, pigen_source_span span,
	const char *word)
{
	size_t length;
	const char *text = pigen_source_span_text(srcs, span, &length);

	return text && length == strlen(word) && !memcmp(text, word, length);
}

/* One parsed pipeline module: the source manager, the parsed syntax tree, the
 * single pipeline node (and its own syntax id), the module node's span and the
 * clocked-process node's span. Free with po_free. */
typedef struct {
	pigen_source_manager sources;
	pigen_source_id source;
	pigen_preprocess_result pre;
	pigen_syntax_tree tree;
	pigen_syntax_error error;
	const pigen_syntax_node *pipeline;
	pigen_syntax_id pipeline_id;
	pigen_source_span module_span;
	pigen_source_span process_span;
	pigen_source_span clk_span;
} parsed;

/* A resolved module skeleton around the parsed pipeline: the semantic model,
 * the module (and its module scope), the clock signal/domain and the clocked
 * process. The caller owns model and must free it. */
typedef struct {
	pigen_semantic_model model;
	pigen_module_id module;
	pigen_scope_id module_scope;
	pigen_symbol_id clk_symbol;
	pigen_signal_id clk;
	pigen_clock_domain_id domain;
	pigen_process_id process;
} unit;

/* The parsed pipeline's identifier token span (the original span the contract
 * anchors the established signal at), read through the token origin. */
static pigen_source_span po_name_span(const parsed *p)
{
	const pigen_expanded_token *token = pigen_expanded_token_get(
		&p->pre.expanded, p->pipeline->as.pipeline.name);

	REQUIRE(token);
	return pigen_origin_spelling_span(&p->pre.expanded, token->origin);
}

/* The parsed pipeline's field (signal-declaration) nodes in source order. */
static size_t po_field_nodes(const parsed *p, const pigen_syntax_node **fields)
{
	const pigen_syntax_node *pipeline = p->pipeline;
	size_t count = 0;
	pigen_syntax_id child;

	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p->tree, child);

		if (node->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION)
			fields[count++] = node;
		child = node->next_sibling;
	}
	return count;
}

/* Parses NAME to a pipeline module and locates its single pipeline node. */
static parsed po_parse(const char *name, const char *text)
{
	parsed p;
	const pigen_syntax_node *root;
	const pigen_syntax_node *module = NULL;
	const pigen_syntax_node *process = NULL;
	const pigen_syntax_node *block;

	memset(&p, 0, sizeof p);
	p.source = pigen_source_add(&p.sources, name, text, strlen(text));
	REQUIRE(!IS_INVALID_ID(p.source));
	{
		pigen_preprocess_error pre_error;

		memset(&pre_error, 0, sizeof pre_error);
		REQUIRE(pigen_preprocess(&p.sources, p.source, NULL, &p.pre, &pre_error));
	}
	REQUIRE(pigen_parse_syntax(&p.pre.expanded, &p.tree, &p.error));
	REQUIRE(!p.error.message);
	root = pigen_syntax_get(&p.tree, (pigen_syntax_id){0});
	REQUIRE(root && root->kind == PIGEN_SYNTAX_COMPILATION_UNIT);
	for (pigen_syntax_id c = root->first_child; c.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p.tree, c);

		REQUIRE(node);
		if (node->kind == PIGEN_SYNTAX_MODULE) module = node;
		c = node->next_sibling;
	}
	REQUIRE(module);
	p.module_span = module->location.source_span;
	for (pigen_syntax_id c = module->first_child; c.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p.tree, c);

		REQUIRE(node);
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS) process = node;
		c = node->next_sibling;
	}
	REQUIRE(process);
	p.process_span = process->location.source_span;
	{
		const pigen_syntax_expr *clock = pigen_syntax_expr_get(
			&p.tree.expressions, process->as.clocked_process.clock);

		REQUIRE(clock);
		p.clk_span = clock->location.source_span;
	}
	block = pigen_syntax_get(&p.tree, process->first_child);
	REQUIRE(block && block->kind == PIGEN_SYNTAX_PROCEDURAL_BLOCK);
	p.pipeline_id = block->first_child;
	p.pipeline = pigen_syntax_get(&p.tree, p.pipeline_id);
	REQUIRE(p.pipeline && p.pipeline->kind == PIGEN_SYNTAX_PIPELINE);
	return p;
}

static void po_free(parsed *p)
{
	pigen_free_syntax_tree(&p->tree);
	pigen_free_preprocess_result(&p->pre);
	pigen_free_sources(&p->sources);
	*p = (parsed){0};
}

/* Builds the resolved module skeleton (module, scope, clock signal/domain and
 * clocked process) for one parsed pipeline module. The clock is the module's
 * `clk` input; the clock domain is its posedge; the module and process spans
 * come from the parsed nodes. */
static void po_unit(const parsed *p, unit *u)
{
	pigen_source_span clk_name;
	pigen_source_span clock_expr;
	pigen_symbol_id module_symbol;
	pigen_declare_result declared;

	memset(u, 0, sizeof *u);
	pigen_semantic_init(&u->model, &p->sources);
	u->model.compilation_scope = pigen_scope_add(&u->model,
		(pigen_scope_id){PIGEN_INVALID_ID},
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
	REQUIRE(!IS_INVALID_ID(u->model.compilation_scope));
	declared = pigen_symbol_declare(&u->model, u->model.compilation_scope,
		PIGEN_SYMBOL_MODULE, (pigen_data_type_id){PIGEN_INVALID_ID},
		p->module_span, p->module_span, &module_symbol, NULL);
	REQUIRE(declared == PIGEN_DECLARE_OK);
	u->module_scope = pigen_scope_add(&u->model, u->model.compilation_scope,
		p->module_span);
	REQUIRE(!IS_INVALID_ID(u->module_scope));
	u->module = pigen_module_add(&u->model, (pigen_syntax_id){1}, module_symbol,
		u->module_scope, p->module_span);
	REQUIRE(!IS_INVALID_ID(u->module));
	/* The clock input `clk`: its name span is the `clk` token read from the
	 * clocked process's clock expression. */
	clk_name = p->clk_span;
	REQUIRE(span_is(&p->sources, clk_name, "clk"));
	{
		pigen_data_type_id t1 = pigen_data_type_sized_logic(&u->model, 1,
			PIGEN_SIGN_UNSIGNED);

		REQUIRE(!IS_INVALID_ID(t1));
		REQUIRE(pigen_symbol_declare(&u->model, u->module_scope,
			PIGEN_SYMBOL_SIGNAL, t1, clk_name, clk_name, &u->clk_symbol,
			NULL) == PIGEN_DECLARE_OK);
		u->clk = pigen_signal_add(&u->model, (pigen_syntax_id){2}, u->module,
			u->clk_symbol, t1, pigen_semantic_scalar_shape(&u->model),
			(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
			PIGEN_SEMANTIC_INPUT, clk_name);
		REQUIRE(!IS_INVALID_ID(u->clk));
		u->domain = pigen_clock_domain_intern(&u->model, u->clk_symbol,
			PIGEN_SEMANTIC_POSEDGE);
		REQUIRE(!IS_INVALID_ID(u->domain));
	}
	clock_expr = (pigen_source_span){p->process_span.source,
		p->process_span.start + 19, p->process_span.start + 22};
	{
		pigen_expr_id clock = pigen_expr_add_symbol(&u->model, u->clk_symbol,
			pigen_data_type_sized_logic(&u->model, 1, PIGEN_SIGN_UNSIGNED),
			clock_expr);

		REQUIRE(!IS_INVALID_ID(clock));
		u->process = pigen_process_add(&u->model, (pigen_syntax_id){3},
			u->module, u->domain, clock, p->process_span);
		REQUIRE(!IS_INVALID_ID(u->process));
	}
}

static void po_free_unit(unit *u)
{
	pigen_free_semantic_model(&u->model);
	*u = (unit){0};
}

/* The expected outgoing packet width: the sum of the parsed pipeline's
 * outgoing field packed widths in source order, read from each field's own
 * declaration text (the [ .. ] range within the field's node span). This is
 * the width of the outgoing packet aggregate the implementation builds; a
 * mis-width, a missing field, or a field's raw (un-aggregated) type fails it.
 * The width, not a fragile data-type-id identity, is the discriminator. */
static size_t po_outgoing_width(const parsed *p)
{
	const pigen_syntax_node *fields[8];
	const char *ftext = p->sources.files ? p->sources.files[0].text : NULL;
	const pigen_syntax_node *field;
	size_t field_count = po_field_nodes(p, fields);
	size_t total = 0;
	size_t i;

	REQUIRE(field_count > 0 && field_count <= 8);
	REQUIRE(ftext);
	for (i = 0; i < field_count; i++)
	{
		pigen_source_span span;
		const char *open;
		const char *close;
		const char *hi;
		const char *lo;
		unsigned long hi_w;
		unsigned long lo_w;

		field = fields[i];
		REQUIRE(field->as.signal_declaration.has_transfer_type);
		REQUIRE(field->as.signal_declaration.transfer_type.transfer_type ==
			PIGEN_TRANSFER_TYPE_BUF);
		span = field->location.source_span;
		/* The field's range is the [ .. ] that lies within the field's own
		 * node span: scan from the span start for a '[' whose matching ']'
		 * also lies within the span. Width is high - low + 1. */
		open = ftext + span.start;
		open = memchr(open, '[', span.end - span.start);
		REQUIRE(open);
		close = strchr(open, ']');
		REQUIRE(close && (size_t)(close - ftext) < span.end);
		hi = open + 1;
		while (*hi == ' ' || *hi == '[') hi++;
		lo = strchr(hi, ':');
		REQUIRE(lo && (size_t)(lo - ftext) < (size_t)(close - ftext));
		lo_w = strtoul(lo + 1, NULL, 10);
		hi_w = strtoul(hi, (char **)NULL, 10);
		total += hi_w - lo_w + 1;
	}
	return total;
}

int main(int argc, char **argv)
{
	check_init(argc, argv);

	/* ------------------------------------------------------------------ */
	/* (1) output-identity: a legal single-field pipeline, once established  */
	/*     through the owner entry, resolves the pipeline identifier to      */
	/*     exactly one module-scope signal whose transfer_type is buf AND    */
	/*     whose direction is OUTPUT. Both are pinned: a buf-but-internal    */
	/*     signal, or an output-but-not-buf signal, fails. Red today at the  */
	/*     establish REQUIRE (the skeleton stub returns -1).                */
	/* ------------------------------------------------------------------ */
	SECTION("output-identity") {
		parsed p = po_parse("single.pigen",
			"module single(input logic clk);\n"
			"\talways @(posedge clk) begin\n"
			"\t\tpipeline single_out begin\n"
			"\t\t\tbit [7:0] a;\n"
			"\t\t\tstage begin\n"
			"\t\t\t\ta <= a;\n"
			"\t\t\tend\n"
			"\t\t\tyield a;\n"
			"\t\tendpipeline\n"
			"\tend\n"
			"endmodule");
		unit u;
		pigen_pipeline_identifier id;
		const pigen_semantic_signal *sig;

		po_unit(&p, &u);
		REQUIRE(pigen_pipeline_identifier_establish(&u.model, p.pipeline_id,
			u.module, p.pipeline, &id) == 0);
		sig = pigen_signal_get(&u.model, id.signal);
		REQUIRE(sig && !IS_INVALID_ID(id.signal));
		REQUIRE(sig->transfer_type == PIGEN_TRANSFER_TYPE_BUF);
		REQUIRE(sig->direction == PIGEN_SEMANTIC_OUTPUT);
		po_free_unit(&u);
		po_free(&p);
	}

	/* ------------------------------------------------------------------ */
	/* (2) outgoing-packet-type: the established signal's data_type is the   */
	/*     outgoing packet aggregate (the field(s) the final yield reads),   */
	/*     not a field's raw type nor a mis-width. Pinned as the packed       */
	/*     width of the established signal's own data_type: a single 8-bit    */
	/*     field gives an 8-bit packet, and a two-field {8-bit, 16-bit}       */
	/*     pipeline gives a 24-bit packet - so the aggregate is the sum of    */
	/*     the outgoing fields, not one field's raw type. A mis-typed or      */
	/*     mis-width outgoing packet fails.                                    */
	/* ------------------------------------------------------------------ */
	SECTION("outgoing-packet-type") {
		/* Single 8-bit field: the outgoing packet is 8 bits. */
		{
			parsed p = po_parse("single.pigen",
				"module single(input logic clk);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline single_out begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= a;\n"
				"\t\t\tend\n"
				"\t\t\tyield a;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			pigen_pipeline_identifier id;
			const pigen_semantic_signal *sig;
			uint64_t width;

			REQUIRE(po_outgoing_width(&p) == 8);
			po_unit(&p, &u);
			REQUIRE(pigen_pipeline_identifier_establish(&u.model, p.pipeline_id,
				u.module, p.pipeline, &id) == 0);
			sig = pigen_signal_get(&u.model, id.signal);
			REQUIRE(sig);
			REQUIRE(pigen_data_type_exists(&u.model, sig->data_type));
			REQUIRE(pigen_const_expr_evaluate_u64(&u.model,
				pigen_data_type_packed_width(&u.model, sig->data_type), &width));
			REQUIRE(width == 8);
			po_free_unit(&u);
			po_free(&p);
		}
		/* Two fields {8-bit, 16-bit}: the outgoing packet is 8+16 = 24 bits,
		 * the concatenation of both outgoing fields - not the last field's
		 * raw 16-bit type and not the first's raw 8-bit type. */
		{
			parsed p = po_parse("twofield.pigen",
				"module twofield(input logic clk);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline twofield_out begin\n"
				"\t\t\tbit [7:0] x;\n"
				"\t\t\tbuf bit [15:0] y;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tx <= x;\n"
				"\t\t\tend\n"
				"\t\t\tyield {x, y};\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			pigen_pipeline_identifier id;
			const pigen_semantic_signal *sig;
			uint64_t width;

			REQUIRE(po_outgoing_width(&p) == 24);
			po_unit(&p, &u);
			REQUIRE(pigen_pipeline_identifier_establish(&u.model, p.pipeline_id,
				u.module, p.pipeline, &id) == 0);
			sig = pigen_signal_get(&u.model, id.signal);
			REQUIRE(sig);
			REQUIRE(pigen_data_type_exists(&u.model, sig->data_type));
			REQUIRE(pigen_const_expr_evaluate_u64(&u.model,
				pigen_data_type_packed_width(&u.model, sig->data_type), &width));
			REQUIRE(width == 24);
			po_free_unit(&u);
			po_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (3) module-scope-binding: the identifier is bound to a module-scope   */
	/*     symbol (recoverable via the module's signal set / symbol and via  */
	/*     pigen_symbol_signal), NOT a stage-private or pipeline-private     */
	/*     identity, and the symbol / signal span is anchored at the         */
	/*     pipeline identifier's original span. A stage-local binding or a    */
	/*     mis-anchored span fails.                                          */
	/* ------------------------------------------------------------------ */
	SECTION("module-scope-binding") {
		parsed p = po_parse("single.pigen",
			"module single(input logic clk);\n"
			"\talways @(posedge clk) begin\n"
			"\t\tpipeline single_out begin\n"
			"\t\t\tbit [7:0] a;\n"
			"\t\t\tstage begin\n"
			"\t\t\t\ta <= a;\n"
			"\t\t\tend\n"
			"\t\t\tyield a;\n"
			"\t\tendpipeline\n"
			"\tend\n"
			"endmodule");
		unit u;
		pigen_pipeline_identifier id;
		const pigen_semantic_module *mod;
		const pigen_symbol *sym;
		const pigen_semantic_signal *sig;
		const pigen_scope *sym_scope;
		pigen_source_span name_span;
		size_t i;
		int found = 0;

		po_unit(&p, &u);
		REQUIRE(pigen_pipeline_identifier_establish(&u.model, p.pipeline_id,
			u.module, p.pipeline, &id) == 0);
		/* Recover the identity through the owner accessor and pin that it is
		 * the same module-scope signal bound to the identifier symbol. */
		{
			pigen_pipeline_identifier again;

			REQUIRE(pigen_pipeline_identifier_of(&u.model, p.pipeline_id,
				u.module, p.pipeline, &again) == 0);
			REQUIRE(again.signal.index == id.signal.index);
			REQUIRE(again.symbol.index == id.symbol.index);
		}
		mod = pigen_module_get(&u.model, u.module);
		REQUIRE(mod);
		sym = pigen_symbol_get(&u.model, id.symbol);
		REQUIRE(sym);
		/* The symbol lives in the module scope, not a stage-/pipeline-local
		 * child scope: its scope IS the module scope and its parent is the
		 * compilation scope. */
		REQUIRE(sym->scope.index == u.module_scope.index);
		sym_scope = pigen_scope_get(&u.model, sym->scope);
		REQUIRE(sym_scope);
		REQUIRE(sym_scope->parent.index == u.model.compilation_scope.index);
		/* The symbol's typed binding recovers the established signal, and the
		 * signal is in the module's signal set. */
		REQUIRE(pigen_symbol_signal(&u.model, id.symbol).index == id.signal.index);
		sig = pigen_signal_get(&u.model, id.signal);
		REQUIRE(sig && sig->symbol.index == id.symbol.index);
		for (i = 0; i < u.model.signal_count; i++)
			if (u.model.signals[i].symbol.index == id.symbol.index)
				found = 1;
		REQUIRE(found);
		/* The span is anchored at the pipeline identifier's original span. */
		name_span = po_name_span(&p);
		REQUIRE(span_is(&p.sources, name_span, "single_out"));
		REQUIRE(sym->name.source.index == name_span.source.index &&
			sym->name.start == name_span.start &&
			sym->name.end == name_span.end);
		REQUIRE(sig->span.source.index == name_span.source.index &&
			sig->span.start == name_span.start &&
			sig->span.end == name_span.end);
		po_free_unit(&u);
		po_free(&p);
	}

	/* ------------------------------------------------------------------ */
	/* (4) buf-field-consistency: every field is a buf and the established    */
	/*     output packet's transfer type is that same buf (omission          */
	/*     canonical and explicit buf both accepted, one dialect). Pinned     */
	/*     from both a single omitted-buf field and an omitted+explicit      */
	/*     buf pair; a non-buf field, or an output packet carrying a          */
	/*     different transfer type, fails.                                   */
	/* ------------------------------------------------------------------ */
	SECTION("buf-field-consistency") {
		/* Omitted-buf canonical form: one field with no transfer annotation. */
		{
			parsed p = po_parse("omitted.pigen",
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
				"endmodule");
			unit u;
			pigen_pipeline_identifier id;
			const pigen_semantic_signal *sig;
			const pigen_syntax_node *fields[8];
			size_t field_count = po_field_nodes(&p, fields);

			REQUIRE(field_count == 1);
			REQUIRE(fields[0]->as.signal_declaration.transfer_type.transfer_type ==
				PIGEN_TRANSFER_TYPE_BUF);
			po_unit(&p, &u);
			REQUIRE(pigen_pipeline_identifier_establish(&u.model, p.pipeline_id,
				u.module, p.pipeline, &id) == 0);
			sig = pigen_signal_get(&u.model, id.signal);
			REQUIRE(sig);
			REQUIRE(sig->transfer_type == PIGEN_TRANSFER_TYPE_BUF);
			po_free_unit(&u);
			po_free(&p);
		}
		/* Omitted + explicit buf: two fields, both buf, output the same buf. */
		{
			parsed p = po_parse("twofield.pigen",
				"module twofield(input logic clk);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline twofield_pipe begin\n"
				"\t\t\tbit [7:0] x;\n"
				"\t\t\tbuf bit [15:0] y;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tx <= x;\n"
				"\t\t\tend\n"
				"\t\t\tyield {x, y};\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			pigen_pipeline_identifier id;
			const pigen_semantic_signal *sig;
			const pigen_syntax_node *fields[8];
			size_t field_count = po_field_nodes(&p, fields);
			size_t i;

			REQUIRE(field_count == 2);
			for (i = 0; i < field_count; i++)
				REQUIRE(fields[i]->as.signal_declaration.transfer_type.transfer_type ==
					PIGEN_TRANSFER_TYPE_BUF);
			po_unit(&p, &u);
			REQUIRE(pigen_pipeline_identifier_establish(&u.model, p.pipeline_id,
				u.module, p.pipeline, &id) == 0);
			sig = pigen_signal_get(&u.model, id.signal);
			REQUIRE(sig);
			REQUIRE(sig->transfer_type == PIGEN_TRANSFER_TYPE_BUF);
			po_free_unit(&u);
			po_free(&p);
		}
	}

	return check_finish();
}
