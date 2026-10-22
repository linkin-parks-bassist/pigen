/* Pipeline stage execution-laws test contract.
 *
 * Test-contract stage of step (b) of the pipeline frontier: the discriminating
 * manifest that pins the SOURCE-LEVEL DISCRIMINABLE stage execution laws on
 * the structured path. It tests the skeleton's owner seam
 * (include/pigen/pipeline_stage.h, pigen_pipeline_stage_laws) against the
 * behavior contract the spec and the stage-law leaves require (spec:
 * what/is/the/spec.md; the stage-execution law how/do/pipeline/stage/reads/work.md;
 * the repartition law how/to/repartition/pipeline/boundaries.md; gates
 * what/are/the/remaining/architecture/migration/gates.md).
 *
 * Four of the six named laws are source-level discriminable and are pinned
 * here, each as a red+anchor discriminating pair:
 *
 *   - the first stage cannot read a pipeline field that is not initialized
 *     (it may only read external or module-scope signals, which are already
 *     live);
 *   - a later stage reads only the immutable incoming fields the previous
 *     stage produced; it does not read a field the chain has not handed it;
 *   - each stage's <= writes an outgoing field that is visible in the
 *     following stage (a <= whose destination is not an outgoing field is
 *     diagnosed);
 *   - adjacent stage packets may differ in member count and widths, but the
 *     complete packed bit streams keep an EQUAL total $bits (leftmost most
 *     significant) - a mismatched total is diagnosed.
 *
 * Two of the six named laws are NOT source-level discriminable and are left to
 * their RTL/behavioral witnesses (consistent with how the packed-bit-stream
 * ORDER law is handled - enforced structurally, pinned by emitted SV, not by a
 * contract-level negative witness); this is a spec-silent fork pending
 * David's decision, not a settled contract:
 *
 *   - "an external consuming read is atomic under the full guard": atomicity
 *     is a temporal property (the stage waits for all inputs and consumes on
 *     advance), not statically discriminable from the per-stage view
 *     (write/read ids + in/out bits); in an unguarded pipeline the complete
 *     guard is the clock edge and every read is under it, so there is no
 *     parseable "non-atomic read" red form;
 *   - "at most one item per cycle": the spec says a stage is "one item OR
 *     SEVERAL items (declarations OR assignments)" - a stage may LEGALLY have
 *     several <= (that is how a multi-field outgoing packet is written, as the
 *     repartition anchor above requires); "one item per cycle" is a
 *     storage-level invariant (one-entry buf, what/is/a/buf.md) with no
 *     parseable source form that violates it without also violating the
 *     equal-$bits law already pinned here.
 *
 * Witness construction: each section parses a legal pipeline through the
 * structured syntax path (pigen_parse_syntax) to obtain the pipeline node and
 * its stage children, builds the surrounding resolved module through the owner
 * APIs (pigen_semantic_init, pigen_module_add, pigen_scope_add,
 * pigen_symbol_declare, pigen_signal_add, pigen_clock_domain_intern,
 * pigen_process_add) - the same module-scope skeleton the resolver builds for
 * a clocked process, since the structured lowerer does not yet carry a pipeline
 * statement through pigen_resolve_semantics (that is a later pipeline step) -
 * then drives the seam entry point and reads back exclusively through the
 * owner accessors (pigen_pipeline_stage_laws + pigen_pipeline_stage_is_invalid,
 * pigen_symbol_lookup, pigen_signal_get, pigen_data_type_packed_width and
 * pigen_const_expr_evaluate_u64). Every witness is located through the parse
 * (the pipeline node, the stage nodes, the nonblocking-assignment nodes, the
 * NAME-expression token origins, the field declarator name spans, the port
 * declarator name spans); every expected width is computed from the parsed
 * node (field widths from the [hi:lo] spans within the field's own declaration
 * span; the total $bits as their sum) - never by a source-text search over the
 * output. No prototype reuse, no editing of the prototype's pipeline witnesses.
 *
 * The manifest is a four-section discriminating manifest wired as one
 * `make pipeline-stage-test T=<section>` entry per section in
 * tests/contracts.json; the Makefile target compiles the test with the exact
 * CFLAGS (-std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude) over
 * tests/pipeline_stage_test.c + src/pipeline_stage.c + src/pipeline_identity.c
 * + the semantic + syntax source lists. Every section compiles and runs today:
 * it fails at its REQUIRE(pigen_pipeline_stage_laws(...) == 0) anchor against
 * the current skeleton stub (which returns -1 and leaves the view invalid),
 * and passes once the law logic lands. Each section independently
 * discriminates its named law: a builder that drops that law turns exactly
 * that section red, and a permissive all-accept builder fails every section.
 */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/data_type.h"
#include "pigen/expression.h"
#include "pigen/pipeline_stage.h"
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
 * clocked-process node's span. Free with ps_free. */
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

/* Parses NAME to a pipeline module and locates its single pipeline node. */
static parsed ps_parse(const char *name, const char *text)
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

static void ps_free(parsed *p)
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
static void ps_unit(const parsed *p, unit *u)
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

static void ps_free_unit(unit *u)
{
	pigen_free_semantic_model(&u->model);
	*u = (unit){0};
}

/* Declares one module-scope input signal for the module port whose
 * declarator name span is WORD, of packed width WIDTH, and returns its
 * module-scope symbol. The port is located through the parse (the module's
 * port signal-declaration children and their declarator name tokens), never
 * by a source-text search. */
static pigen_symbol_id ps_port(const parsed *p, unit *u, const char *word,
	size_t width)
{
	pigen_symbol_id symbol;
	pigen_data_type_id t;
	pigen_source_span name_span;
	const pigen_syntax_node *module;

	module = NULL;
	for (pigen_syntax_id c = pigen_syntax_get(&p->tree, (pigen_syntax_id){0})->first_child;
		c.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p->tree, c);

		REQUIRE(node);
		if (node->kind == PIGEN_SYNTAX_MODULE) module = node;
		c = node->next_sibling;
	}
	REQUIRE(module);
	for (pigen_syntax_id m = module->first_child; m.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *port = pigen_syntax_get(&p->tree, m);
		const pigen_syntax_node *declarator;
		const pigen_expanded_token *token;

		REQUIRE(port);
		if (port->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION)
		{
			declarator = pigen_syntax_get(&p->tree, port->first_child);
			if (declarator &&
				declarator->kind == PIGEN_SYNTAX_SIGNAL_DECLARATOR)
			{
				token = pigen_expanded_token_get(&p->pre.expanded,
					declarator->as.signal_declarator.name);
				REQUIRE(token);
				name_span = pigen_origin_spelling_span(&p->pre.expanded,
					token->origin);
				if (span_is(&p->sources, name_span, word))
				{
					t = pigen_data_type_sized_logic(&u->model, width,
						PIGEN_SIGN_UNSIGNED);
					REQUIRE(!IS_INVALID_ID(t));
					REQUIRE(pigen_symbol_declare(&u->model, u->module_scope,
						PIGEN_SYMBOL_SIGNAL, t, name_span, name_span,
						&symbol, NULL) == PIGEN_DECLARE_OK);
					REQUIRE(!IS_INVALID_ID(pigen_signal_add(&u->model,
						(pigen_syntax_id){(uint32_t)(4 + width)}, u->module,
						symbol, t, pigen_semantic_scalar_shape(&u->model),
						(pigen_expr_id){PIGEN_INVALID_ID},
						PIGEN_TRANSFER_TYPE_LOGIC, PIGEN_SEMANTIC_INPUT,
						name_span)));
					return symbol;
				}
			}
		}
		m = port->next_sibling;
	}
	REQUIRE(0);
	return (pigen_symbol_id){PIGEN_INVALID_ID};
}

/* The parsed pipeline's field (signal-declaration) nodes in source order. */
static size_t ps_field_nodes(const parsed *p, const pigen_syntax_node **fields)
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

/* The parsed pipeline's stage nodes in source order. */
static size_t ps_stage_nodes(const parsed *p, const pigen_syntax_node **stages)
{
	const pigen_syntax_node *pipeline = p->pipeline;
	size_t count = 0;
	pigen_syntax_id child;

	for (child = pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p->tree, child);

		if (node->kind == PIGEN_SYNTAX_STAGE)
			stages[count++] = node;
		child = node->next_sibling;
	}
	return count;
}

/* One stage's item nodes (nonblocking assignments and stage-local
 * declarations) in source order. */
static size_t ps_stage_items(const parsed *p,
	const pigen_syntax_node *stage, const pigen_syntax_node **items)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = stage->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p->tree, child);

		items[count++] = node;
		child = node->next_sibling;
	}
	return count;
}

/* The syntax id of one child node of the stage, located through the parse
 * (the stage's child link that resolves to NODE). The node's own id is the
 * link id, as the tree's own traversal uses it. */
static pigen_syntax_id ps_node_id(const parsed *p,
	const pigen_syntax_node *stage, const pigen_syntax_node *node)
{
	pigen_syntax_id child;

	for (child = stage->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *found = pigen_syntax_get(&p->tree, child);

		if (found == node) return child;
		child = found->next_sibling;
	}
	REQUIRE(0);
	return (pigen_syntax_id){PIGEN_INVALID_ID};
}

/* The syntax id of one child node of the pipeline, located through the parse
 * (the pipeline's child link that resolves to NODE). */
static pigen_syntax_id ps_pipeline_child_id(const parsed *p,
	const pigen_syntax_node *node)
{
	pigen_syntax_id child;

	for (child = p->pipeline->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *found = pigen_syntax_get(&p->tree, child);

		if (found == node) return child;
		child = found->next_sibling;
	}
	REQUIRE(0);
	return (pigen_syntax_id){PIGEN_INVALID_ID};
}

/* The packed width of one pipeline field, read from its own declaration text
 * (the [hi:lo] range within the field's node span). The width, not a
 * source-text search over the output, is the discriminator. */
static size_t ps_field_width(const parsed *p, const pigen_syntax_node *field)
{
	const char *ftext = p->sources.files ? p->sources.files[0].text : NULL;
	pigen_source_span span = field->location.source_span;
	const char *open;
	const char *close;
	const char *hi;
	const char *lo;
	unsigned long hi_w;
	unsigned long lo_w;

	REQUIRE(field->kind == PIGEN_SYNTAX_SIGNAL_DECLARATION);
	REQUIRE(ftext);
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
	return (size_t)(hi_w - lo_w + 1);
}

/* The NAME expression's original spelling span, resolved through the
 * expanded-source token origins. */
static pigen_source_span ps_name_span(const parsed *p,
	pigen_syntax_expr_id expr)
{
	const pigen_syntax_expr *e = pigen_syntax_expr_get(&p->tree.expressions,
		expr);
	const pigen_expanded_token *token;

	REQUIRE(e && e->kind == PIGEN_SYNTAX_EXPR_NAME);
	token = pigen_expanded_token_get(&p->pre.expanded, e->as.atom.token);
	REQUIRE(token);
	return pigen_origin_spelling_span(&p->pre.expanded, token->origin);
}

/* Drives the seam entry point for one stage node of one parsed pipeline on
 * the built model and returns its status (0 accepted, -1 diagnosed). */
static int ps_drives(const parsed *p, unit *u,
	const pigen_syntax_node *stage, pigen_pipeline_stage *out)
{
	return pigen_pipeline_stage_laws(&u->model,
		ps_pipeline_child_id(p, stage), u->module,
		p->pipeline, &p->pre.expanded, out);
}

int main(int argc, char **argv)
{
	check_init(argc, argv);

	/* ------------------------------------------------------------------ */
	/* (1) first-stage-external-reads: the first stage cannot read an        */
	/*     uninitialized pipeline field - it may only read enclosing /       */
	/*     module-scope signals, which are already live. The red forms are a */
	/*     first stage whose <= RHS names a pipeline field (b, not yet       */
	/*     produced by any earlier stage) and the SELF-READ form, a <= whose */
	/*     RHS names the SAME pipeline field it writes: each diagnosed       */
	/*     (rc == -1). The anchor is a first stage reading only the module   */
	/*     input x: accepted (rc == 0), and its view carries the one         */
	/*     outgoing write, the external read and the 8-bit out packet. A     */
	/*     builder that accepts a first-stage field read (self-read included)*/
	/*     or that refuses a first stage reading an enclosing signal, fails  */
	/*     this section.                                                    */
	/* ------------------------------------------------------------------ */
	SECTION("first-stage-external-reads") {
		/* Red: the first stage reads pipeline field b, which no earlier
		 * stage has written - an uninitialized field read. */
		{
			parsed p = ps_parse("fs_red.pigen",
				"module fs_red(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline fs_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= b;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tb <= x;\n"
				"\t\t\tend\n"
				"\t\t\tyield b;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *items[8];
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 2);
			item_count = ps_stage_items(&p, stages[0], items);
			REQUIRE(item_count == 1);
			REQUIRE(items[0]->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			/* The RHS name is the pipeline field b, located through the
			 * parse. */
			{
				pigen_source_span rhs = ps_name_span(&p,
					items[0]->as.nonblocking_assignment.value);

				REQUIRE(span_is(&p.sources, rhs, "b"));
			}
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			rc = ps_drives(&p, &u, stages[0], &view);
			REQUIRE(rc == -1);
			ps_free_unit(&u);
			ps_free(&p);
		}
		/* Red (self-read form): the first stage's <= RHS names the SAME
		 * pipeline field it writes - an uninitialized field read in its
		 * self-read form: diagnosed (rc == -1). */
		{
			parsed p = ps_parse("fs_self_red.pigen",
				"module fs_self_red(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline fs_self_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= a;\n"
				"\t\t\tend\n"
				"\t\t\tyield a;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *items[8];
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 1);
			item_count = ps_stage_items(&p, stages[0], items);
			REQUIRE(item_count == 1);
			REQUIRE(items[0]->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			/* The RHS name is the pipeline field a - the same field the
			 * assignment writes: a self-read, located through the parse. */
			{
				pigen_source_span rhs = ps_name_span(&p,
					items[0]->as.nonblocking_assignment.value);

				REQUIRE(span_is(&p.sources, rhs, "a"));
			}
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			rc = ps_drives(&p, &u, stages[0], &view);
			REQUIRE(rc == -1);
			ps_free_unit(&u);
			ps_free(&p);
		}
		/* Anchor: the first stage reads only the module input x - legal. */
		{
			parsed p = ps_parse("fs_ok.pigen",
				"module fs_ok(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline fs_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tb <= a;\n"
				"\t\t\tend\n"
				"\t\t\tyield b;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *items[8];
			const pigen_syntax_node *fields[8];
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 2);
			REQUIRE(ps_field_nodes(&p, fields) == 2);
			item_count = ps_stage_items(&p, stages[0], items);
			REQUIRE(item_count == 1);
			REQUIRE(items[0]->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			{
				pigen_source_span rhs = ps_name_span(&p,
					items[0]->as.nonblocking_assignment.value);

				REQUIRE(span_is(&p.sources, rhs, "x"));
			}
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			rc = ps_drives(&p, &u, stages[0], &view);
			REQUIRE(rc == 0);
			REQUIRE(!pigen_pipeline_stage_is_invalid(&view));
			/* One outgoing write (a <= x), one consuming external read,
			 * no incoming field yet, 8-bit out packet. */
			REQUIRE(view.write_count == 1);
			REQUIRE(view.read_count == 1);
			REQUIRE(view.in_bits == 0);
			REQUIRE(view.out_bits == ps_field_width(&p, fields[0]));
			/* The outgoing write is the stage's nonblocking assignment,
			 * located through the parse. */
			REQUIRE(view.first_write.index ==
				ps_node_id(&p, stages[0], items[0]).index);
			/* The consuming read resolves to the module-scope input x. */
			{
				pigen_source_span rhs = ps_name_span(&p,
					items[0]->as.nonblocking_assignment.value);
				pigen_symbol_id sym = pigen_symbol_lookup(&u.model,
					u.module_scope, rhs);

				REQUIRE(!IS_INVALID_ID(sym));
				REQUIRE(pigen_symbol_get(&u.model, sym) &&
					pigen_symbol_get(&u.model, sym)->kind ==
					PIGEN_SYMBOL_SIGNAL);
			}
			ps_free_unit(&u);
			ps_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (2) later-stage-incoming-immutable: a later stage reads only the      */
	/*     immutable incoming fields the EARLIER stages wrote - the rule is  */
	/*     any-earlier, not immediately-preceding: the packet accumulates    */
	/*     fields across boundaries (a stage's <= overwrites the named field */
	/*     it carries; unoverwritten fields persist), so a later stage may   */
	/*     read a field written by ANY earlier stage, including one not      */
	/*     written by its immediately-preceding stage. A field NO earlier    */
	/*     stage wrote is diagnosed. The 2-stage anchor is stage 2 reading   */
	/*     field a, written by stage 1: accepted (rc == 0), in/out packet 8  */
	/*     bits, one outgoing write. The red form is stage 2 reading field   */
	/*     b, which NO earlier stage wrote: diagnosed (rc == -1). The        */
	/*     3-stage skip-read anchor is stage 3 reading field a, written only */
	/*     by stage 1 (skipping stage 2): accepted (rc == 0); an             */
	/*     immediately-preceding-only builder rejects it (rc == -1). A       */
	/*     builder that lets a later stage read a field no earlier stage     */
	/*     wrote, or that refuses a field an earlier stage wrote, fails this */
	/*     section.                                                          */
	/* ------------------------------------------------------------------ */
	SECTION("later-stage-incoming-immutable") {
		/* Anchor: stage 2 reads a, the incoming field stage 1 wrote. */
		{
			parsed p = ps_parse("ls_ok.pigen",
				"module ls_ok(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline ls_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tb <= a;\n"
				"\t\t\tend\n"
				"\t\t\tyield b;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *items[8];
			const pigen_syntax_node *fields[8];
			size_t field_count;
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 2);
			field_count = ps_field_nodes(&p, fields);
			REQUIRE(field_count == 2);
			item_count = ps_stage_items(&p, stages[1], items);
			REQUIRE(item_count == 1);
			{
				pigen_source_span rhs = ps_name_span(&p,
					items[0]->as.nonblocking_assignment.value);

				REQUIRE(span_is(&p.sources, rhs, "a"));
			}
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			rc = ps_drives(&p, &u, stages[1], &view);
			REQUIRE(rc == 0);
			REQUIRE(!pigen_pipeline_stage_is_invalid(&view));
			REQUIRE(view.write_count == 1);
			REQUIRE(view.read_count == 0);
			REQUIRE(view.in_bits == ps_field_width(&p, fields[0]));
			REQUIRE(view.out_bits == ps_field_width(&p, fields[1]));
			REQUIRE(view.first_write.index ==
				ps_node_id(&p, stages[1], items[0]).index);
			ps_free_unit(&u);
			ps_free(&p);
		}
		/* Red: stage 2 reads field b - a field NO earlier stage wrote
		 * (stage 1 wrote a, not b): b is not an incoming field. */
		{
			parsed p = ps_parse("ls_red.pigen",
				"module ls_red(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline ls_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tb <= b;\n"
				"\t\t\tend\n"
				"\t\t\tyield b;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *items[8];
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 2);
			item_count = ps_stage_items(&p, stages[1], items);
			REQUIRE(item_count == 1);
			{
				pigen_source_span rhs = ps_name_span(&p,
					items[0]->as.nonblocking_assignment.value);

				REQUIRE(span_is(&p.sources, rhs, "b"));
			}
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			rc = ps_drives(&p, &u, stages[1], &view);
			REQUIRE(rc == -1);
			ps_free_unit(&u);
			ps_free(&p);
		}
		/* Anchor (any-earlier pin): a 3-stage skip-read. Stage 3 reads field
		 * a, which only stage 1 wrote (skipping stage 2, which wrote b): a
		 * field written by an earlier stage, not the immediately-preceding
		 * stage. Accepted (rc == 0) - the packet carries a across the two
		 * boundaries. An immediately-preceding-only builder rejects it
		 * (rc == -1 instead of 0), so this witness discriminates the two
		 * candidate incoming-field rules. */
		{
			parsed p = ps_parse("ls_skip_ok.pigen",
				"module ls_skip_ok(input logic clk, input logic [7:0] x, "
				"input logic [7:0] y);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline ls_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tbit [7:0] c;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tb <= y;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tc <= a;\n"
				"\t\t\tend\n"
				"\t\t\tyield c;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *items[8];
			const pigen_syntax_node *fields[8];
			size_t field_count;
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 3);
			field_count = ps_field_nodes(&p, fields);
			REQUIRE(field_count == 3);
			/* Stage 3 (the third stage) has one item: c <= a, whose RHS is
			 * field a. */
			item_count = ps_stage_items(&p, stages[2], items);
			REQUIRE(item_count == 1);
			{
				pigen_source_span rhs = ps_name_span(&p,
					items[0]->as.nonblocking_assignment.value);

				REQUIRE(span_is(&p.sources, rhs, "a"));
			}
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			ps_port(&p, &u, "y", 8);
			rc = ps_drives(&p, &u, stages[2], &view);
			REQUIRE(rc == 0);
			REQUIRE(!pigen_pipeline_stage_is_invalid(&view));
			/* One outgoing write (c <= a); the incoming read is the
			 * pipeline field a (not an external read), so read_count is 0.
			 * in_bits is field a (the incoming), out_bits is field c (the
			 * outgoing) - equal, so the boundary keeps its total $bits. */
			REQUIRE(view.write_count == 1);
			REQUIRE(view.read_count == 0);
			REQUIRE(view.in_bits == ps_field_width(&p, fields[0]));
			REQUIRE(view.out_bits == ps_field_width(&p, fields[2]));
			/* The outgoing write is stage 3's nonblocking assignment,
			 * located through the parse. */
			REQUIRE(view.first_write.index ==
				ps_node_id(&p, stages[2], items[0]).index);
			ps_free_unit(&u);
			ps_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (3) outgoing-visibility: each stage's <= writes an outgoing field     */
	/*     visible in the following stage. The anchor is a stage whose <=    */
	/*     writes pipeline field a: accepted (rc == 0), with exactly one     */
	/*     outgoing write of 8 bits. The red forms are a <= whose            */
	/*     destination is the module input x (not a pipeline field, so not   */
	/*     an outgoing field) and a <= whose destination is a stage-local    */
	/*     wire (not an outgoing field): each diagnosed (rc == -1). A        */
	/*     builder that accepts a <= to a non-field, or that miscounts the   */
	/*     outgoing write, fails this section.                               */
	/* ------------------------------------------------------------------ */
	SECTION("outgoing-visibility") {
		/* Anchor: the stage's <= writes pipeline field a. */
		{
			parsed p = ps_parse("ov_ok.pigen",
				"module ov_ok(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline ov_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\tend\n"
				"\t\t\tyield a;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *items[8];
			const pigen_syntax_node *fields[8];
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 1);
			REQUIRE(ps_field_nodes(&p, fields) == 1);
			item_count = ps_stage_items(&p, stages[0], items);
			REQUIRE(item_count == 1);
			REQUIRE(items[0]->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			rc = ps_drives(&p, &u, stages[0], &view);
			REQUIRE(rc == 0);
			REQUIRE(!pigen_pipeline_stage_is_invalid(&view));
			REQUIRE(view.write_count == 1);
			REQUIRE(view.out_bits == ps_field_width(&p, fields[0]));
			REQUIRE(view.first_write.index ==
				ps_node_id(&p, stages[0], items[0]).index);
			ps_free_unit(&u);
			ps_free(&p);
		}
		/* Red: the <= destination is the module input x - not a pipeline
		 * field, so it is not an outgoing field. */
		{
			parsed p = ps_parse("ov_red.pigen",
				"module ov_red(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline ov_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tx <= x;\n"
				"\t\t\tend\n"
				"\t\t\tyield a;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *items[8];
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 1);
			item_count = ps_stage_items(&p, stages[0], items);
			REQUIRE(item_count == 1);
			{
				pigen_source_span dest = ps_name_span(&p,
					items[0]->as.nonblocking_assignment.destination);

				REQUIRE(span_is(&p.sources, dest, "x"));
			}
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			rc = ps_drives(&p, &u, stages[0], &view);
			REQUIRE(rc == -1);
			ps_free_unit(&u);
			ps_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (4) repartition-equal-bits: adjacent stage packets may differ in      */
	/*     member count and widths, but the complete packed bit streams must */
	/*     keep an EQUAL total $bits (leftmost most significant). The anchor */
	/*     is a two-field repartition: stage 1 writes fields a (8) and b     */
	/*     (8) - a 16-bit outgoing packet; stage 2 reads {a, b} (16-bit      */
	/*     incoming) and writes field c (16-bit) - equal total $bits at the  */
	/*     boundary: accepted (rc == 0) with in_bits == out_bits == 16. The  */
	/*     red form is a boundary whose total $bits differ (stage 2 reads    */
	/*     {a, b} = 16 bits but writes c = 8 bits): diagnosed (rc == -1). A  */
	/*     builder that widens or narrows the carried packet across a        */
	/*     boundary, or that requires equal member counts, fails this        */
	/*     section.                                                          */
	/* ------------------------------------------------------------------ */
	SECTION("repartition-equal-bits") {
		/* Anchor: equal-$bits two-field repartition. Stage 1 writes a (8)
		 * and b (8) from x and y (16-bit outgoing packet); stage 2 reads
		 * {a, b} (16-bit incoming) and writes c (16-bit) - the complete
		 * packed stream stays 16 bits at the boundary. */
		{
			parsed p = ps_parse("rp_ok.pigen",
				"module rp_ok(input logic clk, input logic [7:0] x, "
				"input logic [7:0] y);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline rp_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tbit [15:0] c;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\t\tb <= y;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tc <= {a, b};\n"
				"\t\t\tend\n"
				"\t\t\tyield c;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *fields[8];
			const pigen_syntax_node *items[8];
			size_t field_count;
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 2);
			field_count = ps_field_nodes(&p, fields);
			REQUIRE(field_count == 3);
			/* Field widths from the parsed declaration spans: a = 8, b = 8,
			 * c = 16. */
			REQUIRE(ps_field_width(&p, fields[0]) == 8);
			REQUIRE(ps_field_width(&p, fields[1]) == 8);
			REQUIRE(ps_field_width(&p, fields[2]) == 16);
			/* Stage 1: two <= items, writes a (8) and b (8) - 16-bit
			 * outgoing packet. */
			item_count = ps_stage_items(&p, stages[0], items);
			REQUIRE(item_count == 2);
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			ps_port(&p, &u, "y", 8);
			rc = ps_drives(&p, &u, stages[0], &view);
			REQUIRE(rc == 0);
			REQUIRE(view.write_count == 2);
			REQUIRE(view.out_bits == 16);
			REQUIRE(view.in_bits == 0);
			/* Stage 2: reads {a, b} (16-bit incoming) and writes c (16-bit)
			 * - equal total $bits at the boundary. */
			item_count = ps_stage_items(&p, stages[1], items);
			REQUIRE(item_count == 1);
			rc = ps_drives(&p, &u, stages[1], &view);
			REQUIRE(rc == 0);
			REQUIRE(!pigen_pipeline_stage_is_invalid(&view));
			REQUIRE(view.write_count == 1);
			REQUIRE(view.in_bits == 16);
			REQUIRE(view.out_bits == 16);
			ps_free_unit(&u);
			ps_free(&p);
		}
		/* Red: a boundary whose total $bits differ. Stage 1 writes a (8)
		 * and b (8) - 16-bit outgoing; stage 2 reads {a, b} (16-bit
		 * incoming) but writes c (8-bit) - the stage carries a 16-bit
		 * incoming but an 8-bit outgoing, a mismatched boundary. */
		{
			parsed p = ps_parse("rp_red.pigen",
				"module rp_red(input logic clk, input logic [7:0] x, "
				"input logic [7:0] y);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline rp_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tbit [7:0] c;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\t\tb <= y;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tc <= {a, b};\n"
				"\t\t\tend\n"
				"\t\t\tyield c;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *fields[8];
			const pigen_syntax_node *items[8];
			size_t field_count;
			size_t item_count;
			pigen_pipeline_stage view;
			int rc;

			REQUIRE(ps_stage_nodes(&p, stages) == 2);
			field_count = ps_field_nodes(&p, fields);
			REQUIRE(field_count == 3);
			REQUIRE(ps_field_width(&p, fields[0]) == 8);
			REQUIRE(ps_field_width(&p, fields[1]) == 8);
			REQUIRE(ps_field_width(&p, fields[2]) == 8);
			item_count = ps_stage_items(&p, stages[1], items);
			REQUIRE(item_count == 1);
			ps_unit(&p, &u);
			ps_port(&p, &u, "x", 8);
			ps_port(&p, &u, "y", 8);
			rc = ps_drives(&p, &u, stages[1], &view);
			REQUIRE(rc == -1);
			ps_free_unit(&u);
			ps_free(&p);
		}
	}


	return check_finish();
}
