/* Pipeline parent-module lowering test contract.
 *
 * Test-contract stage (step (c2)) of step (c) of the pipeline frontier: the
 * discriminating manifest that pins the pipeline parent-module lowering owner
 * (include/pigen/pipeline_lower.h, pigen_pipeline_lower) against the behavior
 * contract the spec and the owning leaves require (spec: what/is/the/spec.md;
 * decomposition: what/is/the/pipeline/parent/module/lowering.md; the buf
 * what/is/a/buf.md; the stage-execution law how/do/pipeline/stage/reads/work.md;
 * the yield what/does/pipeline/yield/mean.md; the guard
 * how/to/reset/and/enable/a/pipeline.md; the member-order law
 * does/the/concatenation/data/type/keying/keep/member/order.md). It is the (c2)
 * boundary of the (c) step: the anchors are RED today against the (c1)
 * fail-loud stub (which validates its arguments and returns -1 without
 * publishing any RTL record) and GREEN once the (c3) implementation lands;
 * the seam reds hold against the stub and under the (c3) implementation,
 * since the non-pipeline-id rejection is part of the full contract.
 *
 * The lowering turns one PIGEN_SYNTAX_PIPELINE statement (a pipeline body
 * inside a clocked process) into the common RTL model of the parent module:
 * one one-entry buf stage storage per stage boundary (a dedicated primitive
 * instance), the per-stage combinational next-value equations over the
 * immutable incoming fields, the guarded ready/valid handshake, and the final
 * yield to the outgoing-packet write (the step-(a) established aggregate of the
 * YIELD-SELECTED field set). The outgoing packet's member order is SOURCE
 * ORDER, the FIRST yielded field the MSB - a width-correct but misordered
 * aggregate is indistinguishable at the data-type and owner/route level and is
 * pinned here structurally in the RTL model's concatenation (the same structure
 * the emitted SV renders), by the outgoing-packet write's child order.
 *
 * Five sections, one `make pipeline-lower-test T=<section>` entry per section
 * in tests/contracts.json:
 *
 *   (1) buf-storage: one one-entry buf storage per stage boundary. The anchor
 *       is a legal 3-stage pipeline that lowers to rc == 0 and publishes
 *       exactly (N-1) = 2 stage-boundary buf storages; the red is a
 *       source-level stage-law violation the seam now reaches - the first
 *       stage reads pipeline field b, which no earlier stage has written
 *       (an uninitialized field read, the step-(b) first-stage law) - driven
 *       through the seam on the real pipeline node (rc == -1).
 *   (2) next-value-equations: per-stage combinational next-value equations over
 *       the immutable incoming fields, the field value visible in the
 *       following stage. The anchor lowers a legal 2-stage pipeline to rc == 0
 *       and asserts the published per-stage next-value equations (one per
 *       stage); the red is a source-level stage-law violation the seam now
 *       reaches - an adjacent stage boundary that keeps UNEQUAL total $bits
 *       (the step-(b) repartition law) - driven through the seam on the real
 *       pipeline node (rc == -1).
 *   (3) yield-write: the final yield writes the outgoing packet (the step-(a)
 *       established aggregate of the YIELD-SELECTED field set). The anchor
 *       lowers a legal pipeline to rc == 0 and asserts some published RTL object
 *       or update destination is bound, by provenance, to the established
 *       outgoing-packet signal; the red form drives a real, non-pipeline
 *       syntax id the seam must reject (rc == -1).
 *   (4) emitted-member-order: the outgoing packet's member order is SOURCE
 *       ORDER, the FIRST yielded field the MSB. The anchor lowers a legal
 *       two-field pipeline {8-bit x, 16-bit y} with `yield {x, y}` to rc == 0
 *       and pins the outgoing-packet write's child order: the leftmost (MSB)
 *       child is the 8-bit first-yielded field x, the rightmost the 16-bit y.
 *   (5) step-b-witnesses: the two step-(b) storage/temporal-law witnesses that
 *       have no route-level negative witness, pinned here as
 *       emission/behavioral witnesses - the one-entry buf's one-item-per-cycle,
 *       and the atomic external-input guard (a false enclosing guard holds all
 *       storage and disables both handshakes). The anchor lowers a legal
 *       pipeline to rc == 0 and asserts the one-entry buf storage (a dedicated
 *       primitive instance) and the guard-gated handshake (the pipeline's
 *       ready/valid equations are published); the red form drives a real,
 *       non-pipeline syntax id the seam must reject (rc == -1).
 *
 * The red form is, per section, the form the lowering must reject (rc == -1)
 * that a legal pipeline does not hit:
 *   - buf-storage and next-value-equations carry a SOURCE-LEVEL stage-law red.
 *     The corrected seam carries the PIGEN_SYNTAX_PIPELINE node and the
 *     expanded source (consistent with the step-(a)/(b) siblings,
 *     pigen_pipeline_identifier_establish / pigen_pipeline_stage_laws), so the
 *     stage facts - the first-stage field reads and the per-boundary total
 *     $bits - are now reachable at this seam, and the step-(b) law owners the
 *     lowering reaches (src/pipeline_stage.c, pigen_pipeline_stage_laws)
 *     define the laws the reds pin. Each red is located through the parse
 *     (the first stage's one nonblocking-assignment item whose RHS is the
 *     unwritten field b; stage 2's one item whose 8-bit destination c follows
 *     16 bits of incoming fields) and driven through the seam on the real
 *     pipeline node: the lowering must reject it. A permissive builder that
 *     accepts the violation lowers it (rc == 0), failing the red; the legal
 *     pipeline still lowers in the anchor, so the red discriminates the law.
 *   - yield-write and step-b-witnesses carry a SEAM red: the lowering is
 *     driven with a real node of the module that is not a
 *     PIGEN_SYNTAX_PIPELINE statement (the module's clocked-process node). The
 *     seam carries the node pointer and validates its kind, so the node-kind
 *     rejection is the seam's own full-contract obligation
 *     (include/pigen/pipeline_lower.h: pipeline must name a
 *     PIGEN_SYNTAX_PIPELINE node) - a well-established identity with a legal
 *     pipeline must lower, so the non-pipeline node must be the form the
 *     lowering itself rejects.
 * emitted-member-order is an anchor-only section: the width-keyed aggregate's
 * member order has no contract-level negative witness (the type is keyed only
 * on summed packed width, the owner/route level is order-insensitive), so the
 * order law is pinned structurally by the emitted SV (does/the/concatenation/
 * data/type/keying/keep/member/order.md) and needs no red form. The anchor's
 * single first stage reads the enclosing module input d (a consuming external
 * read, legal under the step-(b) first-stage law, where a first-stage
 * pipeline-field read - including the self-read the anchor's earlier form
 * carried - is rc == -1), so the anchor is a legal first stage and its rc == 0
 * is reachable alongside the two source-level stage-law reds.
 *
 * Every witness is located through the parse and every expected width is
 * computed from the parsed node (field widths from the [hi:lo] span within the
 * field's own declaration; the total $bits as their sum) - never by a
 * source-text search over the output. No prototype reuse, no editing of any
 * other test file or production source beyond the seam this correction extends.
 *
 * The manifest compiles clean under -Werror via `make pipeline-lower-test`
 * (the Makefile target builds it in a fresh invocation-owned directory with the
 * exact CFLAGS -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude). Its
 * state against the (c1) fail-loud stub: the anchors of buf-storage,
 * next-value-equations, yield-write, emitted-member-order and step-b-witnesses
 * fail at their REQUIRE(rc == 0) - the correct hidden-red state that goes
 * green when the (c3) implementation lands - while every red holds
 * (REQUIRE(rc == -1)): the two source-level stage-law reds (buf-storage,
 * next-value-equations) and the two seam node-kind reds (yield-write,
 * step-b-witnesses). Each section independently discriminates its pinned
 * behavior: a builder that drops the anchor's published records turns exactly
 * that section red, and a builder that accepts a source-level stage-law
 * violation or a non-pipeline node fails that section's red.
 */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/data_type.h"
#include "pigen/expression.h"
#include "pigen/pipeline_identity.h"
#include "pigen/pipeline_lower.h"
#include "pigen/preprocess.h"
#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/rtl_name.h"
#include "pigen/semantic.h"
#include "pigen/source.h"
#include "pigen/sv_emit.h"
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
 * clocked-process node's span. Free with pl_free. */
typedef struct {
	pigen_source_manager sources;
	pigen_source_id source;
	pigen_preprocess_result pre;
	pigen_syntax_tree tree;
	pigen_syntax_error error;
	const pigen_syntax_node *pipeline;
	pigen_syntax_id pipeline_id;
	const pigen_syntax_node *if_statement; /* enclosing if node; NULL if unguarded */
	pigen_syntax_expr_id if_condition; /* its condition; INVALID if unguarded */
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
static parsed pl_parse(const char *name, const char *text)
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
	/* The struct is returned by value; re-anchor the intra-struct pointers
	 * (tree.expanded -> pre.expanded, and pre.expanded.sources -> sources)
	 * to the RETURNED copy so they do not dangle into this frame. */
	p.tree.expanded = &p.pre.expanded;
	p.pre.expanded.sources = &p.sources;
	return p;
}

static void pl_free(parsed *p)
{
	pigen_free_syntax_tree(&p->tree);
	pigen_free_preprocess_result(&p->pre);
	pigen_free_sources(&p->sources);
	*p = (parsed){0};
}

/* Parses NAME to a pipeline module whose single pipeline node is nested under
 * an enclosing procedural guard, `if (guard-expr) begin ... end`, inside the
 * clocked process's procedural block. The guard law (how/to/reset/and/enable/a/
 * pipeline.md; include/pigen/pipeline_lower.h) is the complete enclosing
 * procedural guard: the pipeline's ready/valid equations are all gated by it,
 * and a false guard holds every stage's storage and disables both handshakes.
 * The guarded fixture extension (what/are/the/pipeline/halt/stop/test/
 * scenarios.md, line 1) locates the pipeline under its PIGEN_SYNTAX_IF_STATEMENT
 * ancestor - the procedural block's single statement is the if node, whose
 * `as.if_statement.condition` is the guard's pigen_syntax_expr and whose body
 * is the procedural block holding the pipeline (process -> block -> if ->
 * block -> pipeline) - and exposes the condition expression and the
 * guard-signal name span (through the expanded-source token origins, exactly
 * as pl_name_span resolves a PIGEN_SYNTAX_EXPR_NAME). File-local: it reuses the
 * unguarded pl_parse's source/preprocess/parse path and the established
 * pl_unit/pl_port/pl_drive/pl_name_span helpers unchanged; the step-(a) identity
 * owner (pigen_pipeline_identifier_establish) and the step-(b) stage-law owner
 * read only the pipeline node and its own body, never the parent chain, so a
 * guarded pipeline drives the unchanged 5-arg seam. */
static parsed pl_parse_guarded(const char *name, const char *text)
{
	parsed p;
	const pigen_syntax_node *root;
	const pigen_syntax_node *module = NULL;
	const pigen_syntax_node *process = NULL;
	const pigen_syntax_node *block;
	const pigen_syntax_node *stmt;
	const pigen_syntax_node *if_node;
	const pigen_syntax_node *inner;
	const pigen_syntax_node *pipeline;

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
	/* The block's single statement is the enclosing guard's if node. */
	stmt = pigen_syntax_get(&p.tree, block->first_child);
	REQUIRE(stmt && stmt->kind == PIGEN_SYNTAX_IF_STATEMENT);
	if_node = stmt;
	p.if_statement = if_node;
	p.if_condition = if_node->as.if_statement.condition;
	REQUIRE(!IS_INVALID_ID(p.if_condition));
	/* The if's body is the procedural block holding the single pipeline. */
	inner = pigen_syntax_get(&p.tree, if_node->first_child);
	REQUIRE(inner && inner->kind == PIGEN_SYNTAX_PROCEDURAL_BLOCK);
	pipeline = pigen_syntax_get(&p.tree, inner->first_child);
	REQUIRE(pipeline && pipeline->kind == PIGEN_SYNTAX_PIPELINE);
	p.pipeline_id = (pigen_syntax_id){(uint32_t)(pipeline - p.tree.nodes)};
	p.pipeline = pipeline;
	/* The struct is returned by value; re-anchor the intra-struct pointers
	 * (tree.expanded -> pre.expanded, and pre.expanded.sources -> sources)
	 * to the RETURNED copy so they do not dangle into this frame. */
	p.tree.expanded = &p.pre.expanded;
	p.pre.expanded.sources = &p.sources;
	return p;
}

/* Builds the resolved module skeleton (module, scope, clock signal/domain and
 * clocked process) for one parsed pipeline module. The clock is the module's
 * `clk` input; the clock domain is its posedge; the module and process spans
 * come from the parsed nodes. */
static void pl_unit(const parsed *p, unit *u)
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

static void pl_free_unit(unit *u)
{
	pigen_free_semantic_model(&u->model);
	*u = (unit){0};
}

/* Declares one module-scope input signal for the module port whose declarator
 * name span is WORD, of packed width WIDTH. The port is located through the
 * parse (the module's port signal-declaration children and their declarator
 * name tokens), never by a source-text search. */
static pigen_symbol_id pl_port(const parsed *p, unit *u, const char *word,
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

/* The parsed pipeline's stage nodes in source order. */
static size_t pl_stage_nodes(const parsed *p, const pigen_syntax_node **stages)
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

/* The parsed pipeline's field (signal-declaration) nodes in source order. */
static size_t pl_field_nodes(const parsed *p, const pigen_syntax_node **fields)
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

/* The module's clocked-process node, located through the parse (the module's
 * PIGEN_SYNTAX_CLOCKED_PROCESS child). */
static const pigen_syntax_node *pl_process_node(const parsed *p)
{
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
	for (pigen_syntax_id c = module->first_child; c.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p->tree, c);

		REQUIRE(node);
		if (node->kind == PIGEN_SYNTAX_CLOCKED_PROCESS) return node;
		c = node->next_sibling;
	}
	REQUIRE(0);
	return NULL;
}

/* One stage's item nodes (the nonblocking assignments and stage-local
 * declarations) in source order. */
static size_t pl_stage_items(const parsed *p, const pigen_syntax_node *stage,
	const pigen_syntax_node **items)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = stage->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&p->tree, child);

		REQUIRE(node);
		items[count++] = node;
		child = node->next_sibling;
	}
	return count;
}

/* The packed width of one pipeline field, read from its own declaration text
 * (the [hi:lo] range within the field's node span). The width, not a
 * source-text search over the output, is the discriminator. */
static size_t pl_field_width(const parsed *p, const pigen_syntax_node *field)
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
static pigen_source_span pl_name_span(const parsed *p, pigen_syntax_expr_id expr)
{
	const pigen_syntax_expr *e = pigen_syntax_expr_get(&p->tree.expressions,
		expr);
	const pigen_expanded_token *token;

	REQUIRE(e && e->kind == PIGEN_SYNTAX_EXPR_NAME);
	token = pigen_expanded_token_get(&p->pre.expanded, e->as.atom.token);
	REQUIRE(token);
	return pigen_origin_spelling_span(&p->pre.expanded, token->origin);
}

/* True when the reachable RTL expression subtree rooted at ROOT carries a
 * PIGEN_RTL_EXPR_OBJECT whose origin span spells exactly the guard signal `en`
 * in the parsed source (an owner-text check against the span, never a search
 * over the output). The guarded pipeline's ready equation must carry the guard
 * this way - as the root itself, or as any reachable descendant object - so the
 * assertion holds whether the guard is published directly on the ready or
 * inside a compound guard expression (the complete enclosing procedural guard,
 * how/to/reset/and/enable/a/pipeline.md). */
static int pl_ready_subtree_guard(const pigen_rtl_model *rtl,
	const pigen_rtl_expr *root, const parsed *p)
{
	pigen_rtl_expr_id id = (pigen_rtl_expr_id){(uint32_t)(root - rtl->expressions)};
	const pigen_rtl_expr_id *children;
	size_t child_count;
	size_t i;

	if (root->kind == PIGEN_RTL_EXPR_OBJECT)
	{
		const pigen_rtl_object *obj = pigen_rtl_object_get(rtl, root->object);

		if (obj && span_is(&p->sources, obj->origin, "en"))
			return 1;
	}
	children = pigen_rtl_expr_children(rtl, id, &child_count);
	for (i = 0; i < child_count; i++)
	{
		const pigen_rtl_expr *c = pigen_rtl_expr_get(rtl, children[i]);

		if (c && pl_ready_subtree_guard(rtl, c, p))
			return 1;
	}
	return 0;
}

/* True when the reachable RTL expression subtree rooted at ROOT carries a
 * PIGEN_RTL_EXPR_OBJECT whose origin span spells exactly WORD in the parsed
 * source. The compound-guard section (7) walks the ready's subtree through it
 * once per guard atom of `if (en && f)` to pin that the published guard is
 * the COMPLETE compound expression: a subtree that carries only one atom (or
 * the AND of one atom with the constant 1) fails one of the two walks. */
static int pl_subtree_spells(const pigen_rtl_model *rtl,
	const pigen_rtl_expr *root, const parsed *p, const char *word)
{
	pigen_rtl_expr_id id = (pigen_rtl_expr_id){(uint32_t)(root - rtl->expressions)};
	const pigen_rtl_expr_id *children;
	size_t child_count;
	size_t i;

	if (root->kind == PIGEN_RTL_EXPR_OBJECT)
	{
		const pigen_rtl_object *obj = pigen_rtl_object_get(rtl, root->object);

		if (obj && span_is(&p->sources, obj->origin, word))
			return 1;
	}
	children = pigen_rtl_expr_children(rtl, id, &child_count);
	for (i = 0; i < child_count; i++)
	{
		const pigen_rtl_expr *c = pigen_rtl_expr_get(rtl, children[i]);

		if (c && pl_subtree_spells(rtl, c, p, word))
			return 1;
	}
	return 0;
}

/* True when the reachable RTL expression subtree rooted at ROOT carries a
 * PIGEN_RTL_EXPR_OBJECT whose origin span spells exactly WORD in the parsed
 * source (an owner-text check against the span, never a search over the
 * output). A stage's next-value equation must reference the enclosing input it
 * computes over this way - as the root itself or as any reachable descendant
 * object - so the assertion holds whether the computation names the input
 * directly (the `d` object in the `d + 8'h01` binary) or reaches it through a
 * nested operand. */
static int pl_calc_subtree_refs(const pigen_rtl_model *rtl,
	const pigen_rtl_expr *root, const parsed *p, const char *word)
{
	pigen_rtl_expr_id id = (pigen_rtl_expr_id){(uint32_t)(root - rtl->expressions)};
	const pigen_rtl_expr_id *children;
	size_t child_count;
	size_t i;

	if (root->kind == PIGEN_RTL_EXPR_OBJECT)
	{
		const pigen_rtl_object *obj = pigen_rtl_object_get(rtl, root->object);

		if (obj && span_is(&p->sources, obj->origin, word))
			return 1;
	}
	children = pigen_rtl_expr_children(rtl, id, &child_count);
	for (i = 0; i < child_count; i++)
	{
		const pigen_rtl_expr *c = pigen_rtl_expr_get(rtl, children[i]);

		if (c && pl_calc_subtree_refs(rtl, c, p, word))
			return 1;
	}
	return 0;
}

/* Establishes the step-(a) outgoing-packet identity the lowering consumes as a
 * validated input, fills *ident, and returns the establish status (0 accepted,
 * -1 diagnosed). */
static int pl_establish(const parsed *p, unit *u, pigen_pipeline_identifier *ident)
{
	return pigen_pipeline_identifier_establish(&u->model, p->pipeline_id,
		u->module, p->pipeline, &p->pre.expanded, ident);
}

/* Drives the owner under test (pigen_pipeline_lower) for one parsed pipeline
 * on the built model: it establishes the step-(a) outgoing-packet identity the
 * lowering consumes, then lowers the pipeline statement into the parent
 * module's RTL model. Returns the lowering status (0 accepted, -1 diagnosed).
 * On a diagnosed failure the compile stops and no caller asserts what the
 * failure left behind (how/should/compiler/builders/fail.md). */
static int pl_drive(const parsed *p, unit *u, pigen_rtl_lowering *lowering,
	pigen_rtl_model *rtl)
{
	pigen_pipeline_identifier ident;

	memset(rtl, 0, sizeof *rtl);
	if (pl_establish(p, u, &ident) != 0)
		return -1;
	pigen_rtl_lowering_init(lowering, &u->model, rtl);
	{
		int rc = pigen_pipeline_lower(lowering, u->module, p->pipeline_id,
			p->pipeline, &p->pre.expanded);

		pigen_rtl_lowering_free(lowering);
		return rc;
	}
}

/* Drives the owner under test with a SEAM red: a real node of the module that
 * is not a PIGEN_SYNTAX_PIPELINE statement (the enclosing clocked-process node,
 * located through the parse) is passed as the pipeline node to the seam. The
 * seam carries the node pointer and validates its kind, so the node-kind
 * rejection is now the seam's own full-contract obligation
 * (include/pigen/pipeline_lower.h: pipeline must name a PIGEN_SYNTAX_PIPELINE
 * node) - a well-established identity with a legal pipeline must lower, so the
 * non-pipeline node must be rejected. Returns the lowering status (0 accepted,
 * -1 diagnosed). */
static int pl_seam_red(const parsed *p, unit *u, pigen_rtl_lowering *lowering,
	pigen_rtl_model *rtl)
{
	const pigen_syntax_node *proc = pl_process_node(p);

	memset(rtl, 0, sizeof *rtl);
	pigen_rtl_lowering_init(lowering, &u->model, rtl);
	{
		int rc = pigen_pipeline_lower(lowering, u->module, p->pipeline_id,
			proc, &p->pre.expanded);

		pigen_rtl_lowering_free(lowering);
		return rc;
	}
}

int main(int argc, char **argv)
{
	check_init(argc, argv);

	/* ------------------------------------------------------------------ */
	/* (1) buf-storage: the pipeline's one-entry buf stage storage is        */
	/*     published, one per stage boundary (what/is/a/buf.md; the          */
	/*     intended lowering uses a dedicated primitive instance). The       */
	/*     anchor is a legal 3-stage pipeline that lowers to rc == 0 and     */
	/*     publishes exactly (N-1) = 2 stage-boundary buf storages. The red  */
	/*     is a SOURCE-LEVEL stage-law violation the corrected seam reaches  */
	/*     (it now carries the pipeline node + expanded source): the first   */
	/*     stage reads pipeline field b, which no earlier stage has written  */
	/*     (an uninitialized field read, the step-(b) first-stage law),      */
	/*     located through the parse and driven through the seam on the     */
	/*     real pipeline node (rc == -1). A permissive builder that accepts  */
	/*     a first-stage field read lowers it (rc == 0), failing the red;    */
	/*     a legal pipeline still lowers in the anchor, so the red           */
	/*     discriminates the law. The same law is green and discriminating   */
	/*     where it can be driven on its own owner: pipeline-stage-test,     */
	/*     section first-stage-external-reads. A builder that publishes the  */
	/*     wrong number of stage storages fails this section's anchor.       */
	/* ------------------------------------------------------------------ */
	SECTION("buf-storage") {
		/* Anchor: a legal 3-stage pipeline lowers and publishes two
		 * stage-boundary buf storages (N-1 = 2). */
		{
			parsed p = pl_parse("bs_ok.pigen",
				"module bs_ok(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline bs_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tbit [7:0] c;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tb <= a;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tc <= b;\n"
				"\t\t\tend\n"
				"\t\t\tyield c;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			size_t stage_count;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;

			stage_count = pl_stage_nodes(&p, stages);
			REQUIRE(stage_count == 3);
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* One one-entry buf storage per stage boundary: N-1 = 2. */
			REQUIRE(rtl.instance_count == 2);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
		/* Red: a SOURCE-LEVEL stage-law violation the seam now reaches - the
		 * first stage reads pipeline field b, which no earlier stage has
		 * written (an uninitialized field read, the step-(b) first-stage law).
		 * The violation is located through the parse (the first stage's one
		 * nonblocking-assignment item whose RHS name is b) and driven through
		 * the seam on the real pipeline node: the lowering must reject it
		 * (rc == -1). A permissive builder that accepts a first-stage field
		 * read lowers it (rc == 0), failing this red; a legal pipeline still
		 * lowers in the anchor above, so the red discriminates the law. */
		{
			parsed p = pl_parse("bs_red.pigen",
				"module bs_red(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline bs_pipe begin\n"
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
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			item_count = pl_stage_items(&p, stages[0], items);
			REQUIRE(item_count == 1);
			REQUIRE(items[0]->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			/* The first stage's RHS is pipeline field b, located through the
			 * parse - the field no earlier stage wrote. */
			{
				pigen_source_span rhs = pl_name_span(&p,
					items[0]->as.nonblocking_assignment.value);

				REQUIRE(span_is(&p.sources, rhs, "b"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == -1);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (2) next-value-equations: for each stage the outgoing fields' values  */
	/*     are published as combinational next-value equations over the      */
	/*     immutable incoming fields the earlier stages handed (how/do/      */
	/*     pipeline/stage/reads/work.md), the field value visible in the     */
	/*     following stage without an extra cycle. The anchor lowers a       */
	/*     legal 2-stage pipeline to rc == 0 and asserts the per-stage       */
	/*     next-value equations are published (at least one per stage). The  */
	/*     red is a SOURCE-LEVEL stage-law violation the corrected seam      */
	/*     reaches: an adjacent stage boundary that keeps UNEQUAL total      */
	/*     $bits (the step-(b) repartition law), located through the parse   */
	/*     (stage 2's one item whose 8-bit destination c follows 16 bits of  */
	/*     incoming fields) and driven through the seam on the real          */
	/*     pipeline node (rc == -1). A builder that widens or narrows the    */
	/*     carried packet across a boundary lowers it (rc == 0), failing the */
	/*     red; a legal equal-$bits pipeline still lowers in the anchor, so  */
	/*     the red discriminates the law. The same law is green and          */
	/*     discriminating where it can be driven on its own owner:           */
	/*     pipeline-stage-test, section repartition-equal-bits. A builder    */
	/*     that drops the per-stage next-value equations fails this section's*/
	/*     anchor.                                                          */
	/* ------------------------------------------------------------------ */
	SECTION("next-value-equations") {
		/* Anchor: a legal 2-stage pipeline lowers and publishes the per-stage
		 * combinational next-value equations (at least one per stage). */
		{
			parsed p = pl_parse("nve_ok.pigen",
				"module nve_ok(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline nve_pipe begin\n"
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
			size_t stage_count;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;

			stage_count = pl_stage_nodes(&p, stages);
			REQUIRE(stage_count == 2);
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* Per-stage combinational next-value equations: at least one per
			 * stage (the stage's outgoing fields computed over its immutable
			 * incoming fields). */
			REQUIRE(rtl.equation_count >= stage_count);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
		/* Red: a SOURCE-LEVEL stage-law violation the seam now reaches - an
		 * adjacent stage boundary that keeps UNEQUAL total $bits (the step-(b)
		 * repartition law). Stage 1 writes a (8) and b (8) - a 16-bit outgoing
		 * packet; stage 2 reads {a, b} (16-bit incoming) but writes c (8-bit),
		 * so the boundary carries 16 bits in and 8 bits out. The violation is
		 * located through the parse (stage 2's one nonblocking-assignment item
		 * whose destination c is 8 bits wide while its incoming fields sum to
		 * 16) and driven through the seam on the real pipeline node: the
		 * lowering must reject it (rc == -1). A builder that widens or narrows
		 * the carried packet across a boundary lowers it (rc == 0), failing
		 * this red; a legal equal-$bits pipeline still lowers in the anchor
		 * above, so the red discriminates the law. */
		{
			parsed p = pl_parse("nve_red.pigen",
				"module nve_red(input logic clk, input logic [7:0] x, "
				"input logic [7:0] y);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline nve_pipe begin\n"
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
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			field_count = pl_field_nodes(&p, fields);
			REQUIRE(field_count == 3);
			/* Field widths from the parsed declaration spans: a = 8, b = 8,
			 * c = 8 - the boundary keeps 16 bits in but 8 bits out. */
			REQUIRE(pl_field_width(&p, fields[0]) == 8);
			REQUIRE(pl_field_width(&p, fields[1]) == 8);
			REQUIRE(pl_field_width(&p, fields[2]) == 8);
			item_count = pl_stage_items(&p, stages[1], items);
			REQUIRE(item_count == 1);
			REQUIRE(items[0]->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "y", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == -1);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (3) yield-write: the final yield writes the outgoing packet, the      */
	/*     step-(a) established aggregate of the YIELD-SELECTED field set    */
	/*     (what/does/pipeline/yield/mean.md). The anchor lowers a legal     */
	/*     pipeline to rc == 0 and asserts some published RTL object or      */
	/*     update destination is bound, by provenance, to the established    */
	/*     outgoing-packet signal (the step-(a) identity's buf output); the  */
	/*     red form drives the lowering with the module's clocked-process    */
	/*     node id - a real, non-pipeline syntax id the seam must reject     */
	/*     (rc == -1). The source-level law red this section once carried    */
	/*     (a first stage reading an uninitialized field) is unreachable     */
	/*     at this seam: the stage facts exist only in the syntax tree the   */
	/*     seam does not carry, the pipeline fields are not semantic         */
	/*     signals, the step-(a) owner accepts the red source (establish     */
	/*     returns 0) and the step-(b) law owner is not linked into this     */
	/*     target. The law is green and discriminating where it can be       */
	/*     driven: pipeline-stage-test, section first-stage-external-reads.  */
	/*     A builder that writes the yield to the wrong destination, or that */
	/*     accepts a non-pipeline syntax id, fails this section.             */
	/* ------------------------------------------------------------------ */
	SECTION("yield-write") {
		/* Anchor: a legal pipeline lowers and the final yield writes the
		 * established outgoing-packet signal (the step-(a) buf output). */
		{
			parsed p = pl_parse("yw_ok.pigen",
				"module yw_ok(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline yw_pipe begin\n"
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
			pigen_pipeline_identifier ident;
			const pigen_semantic_signal *sig;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;

			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			REQUIRE(pl_establish(&p, &u, &ident) == 0);
			sig = pigen_signal_get(&u.model, ident.signal);
			REQUIRE(sig && !IS_INVALID_ID(ident.signal));
			/* Drive the owner on the already-established identity (the anchor
			 * established it directly to pin the yield write's destination;
			 * pl_drive would establish it a second time, which the duplicate
			 * symbol declaration rejects). */
			memset(&rtl, 0, sizeof rtl);
			pigen_rtl_lowering_init(&lowering, &u.model, &rtl);
			rc = pigen_pipeline_lower(&lowering, u.module, p.pipeline_id,
				p.pipeline, &p.pre.expanded);
			pigen_rtl_lowering_free(&lowering);
			REQUIRE(rc == 0);
			/* The yield write's destination is the established outgoing-
			 * packet signal: some published RTL object or update is bound,
			 * by provenance, to that step-(a) semantic signal. */
			{
				int found = 0;

				for (i = 0; i < rtl.object_count && !found; i++)
					if (!IS_INVALID_ID(rtl.objects[i].semantic_signal) &&
						rtl.objects[i].semantic_signal.index ==
							ident.signal.index)
						found = 1;
				if (!found)
					for (i = 0; i < rtl.update_count && !found; i++)
					{
						const pigen_rtl_object *dest =
							pigen_rtl_object_get(&rtl,
								rtl.updates[i].destination);

						if (dest && !IS_INVALID_ID(dest->semantic_signal) &&
							dest->semantic_signal.index ==
								ident.signal.index)
							found = 1;
					}
				REQUIRE(found);
			}
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
		/* Red: the lowering driven with a real, non-pipeline syntax id (the
		 * module's clocked-process node) must reject it - the node-kind
		 * rejection is the seam's own full-contract obligation. */
		{
			parsed p = pl_parse("yw_red.pigen",
				"module yw_red(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline yw_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\tend\n"
				"\t\t\tyield a;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;

			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			rc = pl_seam_red(&p, &u, &lowering, &rtl);
			REQUIRE(rc == -1);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (4) emitted-member-order: the outgoing packet's member order is      */
	/*     SOURCE ORDER, the FIRST yielded field the MSB (leftmost child),  */
	/*     the last the LSB (rightmost child). The aggregate is a           */
	/*     width-keyed sized vector, so a width-correct but misordered       */
	/*     aggregate is indistinguishable at the data-type, owner and       */
	/*     route level and has NO contract-level negative witness           */
	/*     (pipeline_lower.h; does/the/concatenation/data/type/keying/keep/ */
	/*     member/order.md). The order dimension is pinned by the terminal  */
	/*     SV emission: the anchor lowers a legal two-field pipeline        */
	/*     {8-bit x, 16-bit y} whose first stage reads the enclosing module */
	/*     input d (a legal first-stage consuming external read - a         */
	/*     first-stage pipeline-field read, including the self-read, is     */
	/*     rc == -1) with `yield {x, y}` to rc == 0, locates the  */
	/*     outgoing-packet write (the RTL equation whose value is the        */
	/*     concatenation) through the owner accessors, names the            */
	/*     concatenation's child objects from their provenance spans and     */
	/*     renders the concatenation through pigen_sv_render_expression      */
	/*     (src/sv_emit.c), pinning the rendered member order. A builder      */
	/*     that emits the reversed order turns exactly this section red;     */
	/*     the other sections are order-insensitive.                          */
	/* ------------------------------------------------------------------ */
	SECTION("emitted-member-order") {
		/* Anchor: a legal two-field pipeline {x[7:0], y[15:0]} whose first
		 * stage reads the enclosing module input d (a consuming external
		 * read, legal under the step-(b) first-stage law - a first-stage
		 * pipeline-field read, including the self-read, is rc == -1) with
		 * `yield {x, y}` lowers to rc == 0 and the outgoing-packet
		 * concatenation renders, through the terminal SV emission, with the
		 * member order in SOURCE ORDER: the first-yielded field x (8-bit)
		 * the leftmost (MSB) child, the second field y (16-bit) the
		 * rightmost (LSB) child. */
		{
			parsed p = pl_parse("emo_ok.pigen",
				"module emo_ok(input logic clk, input logic [7:0] x, input logic [7:0] d);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline emo_pipe begin\n"
				"\t\t\tbit [7:0] x;\n"
				"\t\t\tbuf bit [15:0] y;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tx <= d;\n"
				"\t\t\tend\n"
				"\t\t\tyield {x, y};\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			pigen_rtl_expr_id write = (pigen_rtl_expr_id){PIGEN_INVALID_ID};
			const pigen_rtl_expr_id *children;
			size_t child_count = 0;
			size_t i;

			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "d", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* Locate the outgoing-packet write through the owner accessors:
			 * the RTL equation (or clocked update) whose value is the
			 * outgoing-packet aggregate concatenation. */
			for (i = 0; i < rtl.equation_count && IS_INVALID_ID(write); i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;

				if (v && v->kind == PIGEN_RTL_EXPR_CONCATENATION)
					write = eq->value;
			}
			if (IS_INVALID_ID(write))
				for (i = 0; i < rtl.update_count && IS_INVALID_ID(write); i++)
				{
					const pigen_rtl_update *up = pigen_rtl_update_get(&rtl,
						(pigen_rtl_update_id){(uint32_t)i});
					const pigen_rtl_expr *v = up ? pigen_rtl_expr_get(&rtl,
						up->value) : NULL;

					if (v && v->kind == PIGEN_RTL_EXPR_CONCATENATION)
						write = up->value;
				}
			REQUIRE(!IS_INVALID_ID(write));
			children = pigen_rtl_expr_children(&rtl, write, &child_count);
			REQUIRE(children && child_count == 2);
			/* Name the concatenation's child objects from their provenance
			 * spans (SOURCE role) so the terminal renderer can spell them. */
			{
				pigen_rtl_name_request requests[8];
				size_t request_count = 0;
				char *rendered = (char *)"sentinel";
				const char *x_at;
				const char *y_at;

				for (i = 0; i < child_count; i++)
				{
					const pigen_rtl_expr *c = pigen_rtl_expr_get(&rtl,
						children[i]);

					if (!c || c->kind != PIGEN_RTL_EXPR_OBJECT)
						continue;
					requests[request_count].kind = PIGEN_RTL_NAME_SOURCE;
					requests[request_count].origin =
						pigen_rtl_object_get(&rtl, c->object)->origin;
					request_count++;
					REQUIRE(request_count <= 8);
				}
				if (request_count)
					REQUIRE(pigen_rtl_assign_names(&rtl, &p.sources, requests,
						request_count).index != PIGEN_INVALID_ID);
				/* Render the outgoing-packet concatenation through the
				 * terminal SV emission and pin the member order: the
				 * first-yielded field (x, 8-bit) renders as the leftmost
				 * (MSB) child, the second field (y, 16-bit) as the rightmost
				 * (LSB) child. A reversed order ({y,x}) fails this pin. */
				REQUIRE(pigen_sv_render_expression(&rtl,
					pigen_rtl_expr_get(&rtl, write), &rendered) == 0);
				REQUIRE(rendered != NULL && rendered != (char *)"sentinel");
				x_at = strstr(rendered, "{x,y}");
				REQUIRE(x_at != NULL);
				y_at = strstr(rendered, "{y,x}");
				REQUIRE(y_at == NULL);
				free(rendered);
			}
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (5) step-b-witnesses: the two step-(b) storage/temporal-law witnesses */
	/*     that have no route-level negative witness, pinned here as         */
	/*     emission/behavioral witnesses (how/to/reset/and/enable/a/        */
	/*     pipeline.md; how/do/pipeline/stage/reads/work.md):               */
	/*       (a) the one-entry buf's one-item-per-cycle: the stage storage  */
	/*           is ONE-entry elastic storage (a dedicated one-entry buf     */
	/*           primitive instance), so at most one item is held per        */
	/*           cycle;                                                    */
	/*       (b) the atomic external-input guard: the complete enclosing     */
	/*           procedural guard enables the pipeline; a false guard holds  */
	/*           all storage and disables both the input and the output      */
	/*           handshake.                                                */
	/*     The anchor lowers a legal pipeline to rc == 0 and asserts the     */
	/*     one-entry buf storage (a dedicated primitive instance) and the    */
	/*     guard-gated handshake (the pipeline's ready/valid equations are   */
	/*     published) are present; the red form drives the lowering with the */
	/*     module's clocked-process node id - a real, non-pipeline syntax id */
	/*     the seam must reject (rc == -1). The source-level law red this    */
	/*     section once carried (a first stage reading an uninitialized      */
	/*     field) is unreachable at this seam: the stage facts exist only    */
	/*     in the syntax tree the seam does not carry, the pipeline fields   */
	/*     are not semantic signals, the step-(a) owner accepts the red      */
	/*     source (establish returns 0) and the step-(b) law owner is not    */
	/*     linked into this target. The law is green and discriminating      */
	/*     where it can be driven: pipeline-stage-test, section             */
	/*     first-stage-external-reads. A builder that drops the storage or   */
	/*     handshake witnesses, or that accepts a non-pipeline syntax id,    */
	/*     fails this section.                                              */
	/* ------------------------------------------------------------------ */
	SECTION("step-b-witnesses") {
		/* Anchor: a legal 2-stage pipeline lowers and publishes the one-entry
		 * buf stage storage (a dedicated one-entry primitive instance) and the
		 * guard-gated handshake (the pipeline's ready/valid equations exist). */
		{
			parsed p = pl_parse("sbw_ok.pigen",
				"module sbw_ok(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline sbw_pipe begin\n"
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
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;

			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* (a) one-item-per-cycle: the stage storage is a dedicated
			 * one-entry buf primitive instance (one per stage boundary,
			 * N-1 = 1). */
			REQUIRE(rtl.instance_count >= 1);
			/* (b) atomic external-input guard: the guarded ready/valid
			 * handshake is published as RTL equations (the pipeline's
			 * ready/valid equations, gated by the complete enclosing
			 * guard). */
			REQUIRE(rtl.equation_count >= 1);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
		/* Red: the lowering driven with a real, non-pipeline syntax id (the
		 * module's clocked-process node) must reject it - the node-kind
		 * rejection is the seam's own full-contract obligation. */
		{
			parsed p = pl_parse("sbw_red.pigen",
				"module sbw_red(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline sbw_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\tend\n"
				"\t\t\tyield a;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;

			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			rc = pl_seam_red(&p, &u, &lowering, &rtl);
			REQUIRE(rc == -1);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) ready-guarded-by-en: the pipeline's ready equation is gated by  */
	/*     the complete enclosing procedural guard (how/to/reset/and/     */
	/*     enable/a/pipeline.md; include/pigen/pipeline_lower.h guard law) */
	/*     rather than by the constant-1 stand-in the current lowering     */
	/*     publishes (src/pipeline_lower.c: guard = one, ready = guard).   */
	/*     The anchor is the guarded fixture of the accepted pipeline      */
	/*     halting/stop scenario list (what/are/the/pipeline/halt/stop/   */
	/*     test/scenarios.md line 1, reviewed at what/is/the/review/of/   */
	/*     the/pipeline/halt/stop/test/scenarios.md): a legal 2-stage      */
	/*     pipeline nested under `if (en)` inside the clocked process      */
	/*     (1-bit module input en, stage 1 a <= x, stage 2 b <= a, yield  */
	/*     b). It lowers to rc == 0 and the pipeline's ready equation      */
	/*     publishes a 1-bit value that is NOT an integer constant         */
	/*     (PIGEN_RTL_EXPR_INTEGER) and that IS, or contains in its        */
	/*     reachable expression subtree, a PIGEN_RTL_EXPR_OBJECT whose     */
	/*     origin span spells exactly `en` in the parsed source.           */
	/*     Against the current master lowering the ready equation's value  */
	/*     is the integer constant 1, so no published 1-bit handshake      */
	/*     equation carries the guard object and this section is the       */
	/*     discriminating red: it FAILS here and turns green only when the */
	/*     guard-replacement implementation (the later owner, plan item 8) */
	/*     publishes the ready gated by the enclosing guard's expression.  */
	/*     The guard-signal name span is pinned through the expanded-      */
	/*     source token origins (exactly as pl_name_span resolves the if   */
	/*     condition's name), so the assertion is against the parsed       */
	/*     source, never a search over the output. The guarded-fixture     */
	/*     extension (pl_parse_guarded) is the file-local seam the sibling */
	/*     lines 2, 4, 5, 6 reuse; the step-(a) identity and step-(b)      */
	/*     stage-law owners read only the pipeline node and its body, so   */
	/*     the guarded pipeline drives the unchanged 5-arg seam.           */
	/* ------------------------------------------------------------------ */
	SECTION("ready-guarded-by-en") {
		/* Anchor: a legal 2-stage pipeline under `if (en)` lowers and its
		 * ready equation is gated by the guard signal, not the constant-1
		 * stand-in. */
		{
			parsed p = pl_parse_guarded("gre_ok.pigen",
				"module gre_ok(input logic clk, input logic en, "
				"input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tif (en) begin\n"
				"\t\t\tpipeline gre_pipe begin\n"
				"\t\t\t\tbit [7:0] a;\n"
				"\t\t\t\tbit [7:0] b;\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\ta <= x;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\tb <= a;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tyield b;\n"
				"\t\t\tendpipeline\n"
				"\t\tend\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			int found = 0;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			/* The guarded-fixture extension: the pipeline sits under its
			 * PIGEN_SYNTAX_IF_STATEMENT ancestor and the if's condition
			 * resolves, through the expanded-source token origins, to a
			 * name spelling exactly `en` (the guard signal). This pins the
			 * fixture and the guard span the ready assertion checks for. */
			REQUIRE(p.if_statement &&
				p.if_statement->kind == PIGEN_SYNTAX_IF_STATEMENT);
			REQUIRE(!IS_INVALID_ID(p.if_condition));
			{
				const pigen_syntax_expr *cond = pigen_syntax_expr_get(
					&p.tree.expressions, p.if_condition);
				pigen_source_span gspan;

				REQUIRE(cond && cond->kind == PIGEN_SYNTAX_EXPR_NAME);
				gspan = pl_name_span(&p, p.if_condition);
				REQUIRE(span_is(&p.sources, gspan, "en"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "en", 1);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* The pipeline's ready equation is a published RTL equation
			 * whose value is a 1-bit expression. Locate the one that is NOT
			 * an integer constant and whose reachable expression subtree
			 * carries an object spelling exactly `en`: the ready gated by
			 * the complete enclosing procedural guard. Against the current
			 * master lowering the ready value is the integer constant 1, so
			 * no such equation exists and found stays 0 (the red). */
			for (i = 0; i < rtl.equation_count && !found; i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;
				const pigen_rtl_type *vt = v ? pigen_rtl_type_get(&rtl,
					v->type) : NULL;

				if (!v || !vt || vt->width != 1)
					continue;
				if (v->kind == PIGEN_RTL_EXPR_INTEGER)
					continue;
				found = pl_ready_subtree_guard(&rtl, v, &p);
			}
			REQUIRE(found);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (7) ready-not-constant-one: the guarded pipeline's ready/valid      */
	/*     handshake is NOT the constant-1 stand-in. The anchor is the     */
	/*     same guarded pipeline as the accepted pipeline halting/stop     */
	/*     scenario list (what/are/the/pipeline/halt/stop/test/           */
	/*     scenarios.md line 2, reviewed at what/is/the/review/of/the/    */
	/*     pipeline/halt/stop/test/scenarios.md): a legal 2-stage          */
	/*     pipeline nested under `if (en)` inside the clocked process      */
	/*     (1-bit module input en, stage 1 a <= x, stage 2 b <= a, yield   */
	/*     b). It lowers to rc == 0 and NO published 1-bit handshake       */
	/*     equation (the pipeline's ready/valid equations) has an          */
	/*     integer-constant value (PIGEN_RTL_EXPR_INTEGER) - the           */
	/*     constant-1 stand-in the manifest must now discriminate. The     */
	/*     handshake must still be published (at least one 1-bit equation) */
	/*     so the exclusion is not vacuous: a lowering that drops the      */
	/*     ready equation instead of gating it fails the presence pin, and */
	/*     a lowering that keeps guard = constant 1 (the current           */
	/*     src/pipeline_lower.c: guard = one, ready = guard, so the        */
	/*     published ready equation is ready = 1) fails the exclusion.     */
	/*     This section is the discriminating red: it FAILS against the    */
	/*     current master lowering and passes if and only if no published  */
	/*     1-bit handshake equation carries an integer-constant value,     */
	/*     i.e. once the guard-replacement implementation publishes the    */
	/*     ready gated by the enclosing guard's expression. The per-stage  */
	/*     next-value stand-in constants are packet-width (not 1-bit) and  */
	/*     are a separate scoped obligation (the stage-computation list),  */
	/*     so they do not trip this 1-bit exclusion. The guarded-fixture   */
	/*     extension (pl_parse_guarded) is the file-local seam the         */
	/*     sibling lines 1, 4, 5, 6 share; the step-(a) identity and       */
	/*     step-(b) stage-law owners read only the pipeline node and its   */
	/*     body, so the guarded pipeline drives the unchanged 5-arg seam.  */
	/* ------------------------------------------------------------------ */
	SECTION("ready-not-constant-one") {
		/* Anchor: a legal 2-stage pipeline under `if (en)` lowers and none
		 * of its published 1-bit handshake equations is an integer constant. */
		{
			parsed p = pl_parse_guarded("rnco_ok.pigen",
				"module rnco_ok(input logic clk, input logic en, "
				"input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tif (en) begin\n"
				"\t\t\tpipeline rnco_pipe begin\n"
				"\t\t\t\tbit [7:0] a;\n"
				"\t\t\t\tbit [7:0] b;\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\ta <= x;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\tb <= a;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tyield b;\n"
				"\t\t\tendpipeline\n"
				"\t\tend\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			size_t bit1_count = 0;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			/* The guarded-fixture extension: the pipeline sits under its
			 * PIGEN_SYNTAX_IF_STATEMENT ancestor and the if's condition
			 * resolves, through the expanded-source token origins, to a
			 * name spelling exactly `en` (the guard signal). */
			REQUIRE(p.if_statement &&
				p.if_statement->kind == PIGEN_SYNTAX_IF_STATEMENT);
			REQUIRE(!IS_INVALID_ID(p.if_condition));
			{
				const pigen_syntax_expr *cond = pigen_syntax_expr_get(
					&p.tree.expressions, p.if_condition);
				pigen_source_span gspan;

				REQUIRE(cond && cond->kind == PIGEN_SYNTAX_EXPR_NAME);
				gspan = pl_name_span(&p, p.if_condition);
				REQUIRE(span_is(&p.sources, gspan, "en"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "en", 1);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* Inspect every published 1-bit handshake equation (the
			 * pipeline's ready/valid equations): at least one must be
			 * published (the handshake is present, so the exclusion below
			 * is not vacuous) and none may carry an integer-constant value.
			 * Against the current master lowering the ready equation's value
			 * is the integer constant 1 (the constant-1 stand-in), so the
			 * exclusion REQUIRE fails - the discriminating red. It passes if
			 * and only if the guard-replacement implementation publishes the
			 * ready gated by the enclosing guard's expression, never a
			 * constant. The per-stage next-value stand-in constants are
			 * packet-width (8-bit here), not 1-bit, and belong to the
			 * separate stage-computation obligation. */
			for (i = 0; i < rtl.equation_count; i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;
				const pigen_rtl_type *vt = v ? pigen_rtl_type_get(&rtl,
					v->type) : NULL;

				if (!v || !vt || vt->width != 1)
					continue;
				bit1_count++;
				REQUIRE(v->kind != PIGEN_RTL_EXPR_INTEGER);
			}
			REQUIRE(bit1_count >= 1);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) unguarded-ready-constant-one: an UNGUARDED pipeline - a legal  */
	/*     pipeline placed directly in the procedural block, with no      */
	/*     enclosing procedural guard (no `if`) - is enabled, and its     */
	/*     ready stays the constant 1. This is the boundary that keeps the */
	/*     guard-replacement implementation from over-gating: an          */
	/*     unguarded pipeline has no enclosing procedural guard to gate    */
	/*     its ready by, so its ready must remain the constant 1 rather    */
	/*     than being gated down to some other 1-bit constant (the         */
	/*     false-guard hold value is 0, which over-gating would publish).  */
	/*     The anchor lowers a legal 2-stage pipeline (stage 1 `a <= x`,  */
	/*     stage 2 `b <= a`, `yield b`) to rc == 0 and locates the        */
	/*     pipeline's ready equation through the owner accessors, pinning  */
	/*     its value: a 1-bit PIGEN_RTL_EXPR_INTEGER with value 1. The     */
	/*     ready value is the ONLY 1-bit integer-constant equation value   */
	/*     the lowering may publish - every other 1-bit integer-constant   */
	/*     equation value must also be 1, so a lowering that over-gates the */
	/*     unguarded ready to the constant 0 (or any other 1-bit constant) */
	/*     fails this section. The same fixture shape the other sections   */
	/*     use, reached through the established unguarded seam helpers     */
	/*     (pl_parse/pl_unit/pl_port/pl_drive) unchanged.                 */
	/* ------------------------------------------------------------------ */
	SECTION("unguarded-ready-constant-one") {
		/* Anchor: a legal 2-stage pipeline directly in the procedural block
		 * (no enclosing if statement) lowers to rc == 0 and its ready
		 * equation publishes the constant-1 value (a 1-bit
		 * PIGEN_RTL_EXPR_INTEGER, value 1) - an unguarded pipeline is
		 * enabled, and its ready remains the constant 1. */
		{
			parsed p = pl_parse("ugc_ok.pigen",
				"module ugc_ok(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline ugc_pipe begin\n"
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
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;

			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* The ready equation's value is a 1-bit PIGEN_RTL_EXPR_INTEGER
			 * with value 1. The ready value is the only 1-bit
			 * integer-constant equation value the lowering publishes, so
			 * locate it by exactly that shape: a 1-bit integer-constant
			 * value that IS the constant 1. A missing ready (no such
			 * equation) or an over-gated ready (a 1-bit integer-constant
			 * value that is not 1) fails. */
			{
				int found = 0;

				for (i = 0; i < rtl.equation_count; i++)
				{
					const pigen_rtl_equation *eq = pigen_rtl_equation_get(
						&rtl, (pigen_rtl_equation_id){(uint32_t)i});
					const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
						eq->value) : NULL;
					const pigen_rtl_type *t;

					if (!v || v->kind != PIGEN_RTL_EXPR_INTEGER)
						continue;
					t = pigen_rtl_type_get(&rtl, v->type);
					if (!t || t->width != 1)
						continue;
					/* Adversarial: no 1-bit integer-constant equation value
					 * may be anything other than the constant 1. An
					 * over-gating lowering that drives the unguarded ready to
					 * the constant 0 (the false-guard hold value) or any other
					 * 1-bit constant publishes such a value and fails here, so
					 * the ready is pinned to the constant 1 and to nothing
					 * else. */
					REQUIRE(v->value == 1);
					found = 1;
				}
				REQUIRE(found);
			}
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) ready-guarded-by-en: the pipeline's ready equation is gated by  */
	/*     the complete enclosing procedural guard (how/to/reset/and/     */
	/*     enable/a/pipeline.md; include/pigen/pipeline_lower.h guard law) */
	/*     rather than by the constant-1 stand-in the current lowering     */
	/*     publishes (src/pipeline_lower.c: guard = one, ready = guard).   */
	/*     The anchor is the guarded fixture of the accepted pipeline      */
	/*     halting/stop scenario list (what/are/the/pipeline/halt/stop/   */
	/*     test/scenarios.md line 1, reviewed at what/is/the/review/of/   */
	/*     the/pipeline/halt/stop/test/scenarios.md): a legal 2-stage      */
	/*     pipeline nested under `if (en)` inside the clocked process      */
	/*     (1-bit module input en, stage 1 a <= x, stage 2 b <= a, yield  */
	/*     b). It lowers to rc == 0 and the pipeline's ready equation      */
	/*     publishes a 1-bit value that is NOT an integer constant         */
	/*     (PIGEN_RTL_EXPR_INTEGER) and that IS, or contains in its        */
	/*     reachable expression subtree, a PIGEN_RTL_EXPR_OBJECT whose     */
	/*     origin span spells exactly `en` in the parsed source.           */
	/*     Against the current master lowering the ready equation's value  */
	/*     is the integer constant 1, so no published 1-bit handshake      */
	/*     equation carries the guard object and this section is the       */
	/*     discriminating red: it FAILS here and turns green only when the */
	/*     guard-replacement implementation (the later owner, plan item 8) */
	/*     publishes the ready gated by the enclosing guard's expression.  */
	/*     The guard-signal name span is pinned through the expanded-      */
	/*     source token origins (exactly as pl_name_span resolves the if   */
	/*     condition's name), so the assertion is against the parsed       */
	/*     source, never a search over the output. The guarded-fixture     */
	/*     extension (pl_parse_guarded) is the file-local seam the sibling */
	/*     lines 2, 4, 5, 6 reuse; the step-(a) identity and step-(b)      */
	/*     stage-law owners read only the pipeline node and its body, so   */
	/*     the guarded pipeline drives the unchanged 5-arg seam.           */
	/* ------------------------------------------------------------------ */
	SECTION("ready-guarded-by-en") {
		/* Anchor: a legal 2-stage pipeline under `if (en)` lowers and its
		 * ready equation is gated by the guard signal, not the constant-1
		 * stand-in. */
		{
			parsed p = pl_parse_guarded("gre_ok.pigen",
				"module gre_ok(input logic clk, input logic en, "
				"input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tif (en) begin\n"
				"\t\t\tpipeline gre_pipe begin\n"
				"\t\t\t\tbit [7:0] a;\n"
				"\t\t\t\tbit [7:0] b;\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\ta <= x;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\tb <= a;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tyield b;\n"
				"\t\t\tendpipeline\n"
				"\t\tend\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			int found = 0;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			/* The guarded-fixture extension: the pipeline sits under its
			 * PIGEN_SYNTAX_IF_STATEMENT ancestor and the if's condition
			 * resolves, through the expanded-source token origins, to a
			 * name spelling exactly `en` (the guard signal). This pins the
			 * fixture and the guard span the ready assertion checks for. */
			REQUIRE(p.if_statement &&
				p.if_statement->kind == PIGEN_SYNTAX_IF_STATEMENT);
			REQUIRE(!IS_INVALID_ID(p.if_condition));
			{
				const pigen_syntax_expr *cond = pigen_syntax_expr_get(
					&p.tree.expressions, p.if_condition);
				pigen_source_span gspan;

				REQUIRE(cond && cond->kind == PIGEN_SYNTAX_EXPR_NAME);
				gspan = pl_name_span(&p, p.if_condition);
				REQUIRE(span_is(&p.sources, gspan, "en"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "en", 1);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* The pipeline's ready equation is a published RTL equation
			 * whose value is a 1-bit expression. Locate the one that is NOT
			 * an integer constant and whose reachable expression subtree
			 * carries an object spelling exactly `en`: the ready gated by
			 * the complete enclosing procedural guard. Against the current
			 * master lowering the ready value is the integer constant 1, so
			 * no such equation exists and found stays 0 (the red). */
			for (i = 0; i < rtl.equation_count && !found; i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;
				const pigen_rtl_type *vt = v ? pigen_rtl_type_get(&rtl,
					v->type) : NULL;

				if (!v || !vt || vt->width != 1)
					continue;
				if (v->kind == PIGEN_RTL_EXPR_INTEGER)
					continue;
				found = pl_ready_subtree_guard(&rtl, v, &p);
			}
			REQUIRE(found);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (7) compound-guard-published: the ready equation of a pipeline      */
	/*     guarded by a COMPOUND guard must publish the COMPLETE enclosing  */
	/*     procedural guard expression - not a single atom and not a        */
	/*     constant (how/to/reset/and/enable/a/pipeline.md; the guard law   */
	/*     in include/pigen/pipeline_lower.h). The anchor is the line-4     */
	/*     scenario of the accepted pipeline halting/stop list (what/are/   */
	/*     the/pipeline/halt/stop/test/scenarios.md, reviewed at what/is/   */
	/*     the/review/of/the/pipeline/halt/stop/test/scenarios.md, list     */
	/*     pinned fe0e0742): a legal 2-stage pipeline nested under          */
	/*     `if (en && f)` inside the clocked process (1-bit module inputs   */
	/*     en and f, stage 1 a <= x, stage 2 b <= a, yield b). It lowers    */
	/*     to rc == 0 and the pipeline's ready equation value is a binary   */
	/*     logical-AND expression (PIGEN_RTL_EXPR_BINARY,                  */
	/*     PIGEN_BINARY_LOGICAL_AND) whose reachable object children        */
	/*     include objects whose origin spans spell exactly `en` and `f`.   */
	/*     Against the current master lowering the ready equation's value   */
	/*     is the integer constant 1 (src/pipeline_lower.c: guard = one),   */
	/*     so no 1-bit binary logical-AND ready exists and this section is */
	/*     the discriminating red: it FAILS here and turns green only when */
	/*     the guard-replacement implementation (the later owner, plan      */
	/*     item 8) publishes the ready gated by the complete compound       */
	/*     guard. A replacement that publishes only ONE atom of the guard   */
	/*     (or the AND of one atom with the constant 1) fails the          */
	/*     both-objects assertion; a replacement that publishes a single    */
	/*     atom without the AND fails the binary-kind assertion. The guard  */
	/*     spans are pinned through the parsed condition (its operator and */
	/*     its two name children, resolved exactly as pl_name_span resolves*/
	/*     a name), never a search over the output. This section reuses the*/
	/*     file-local guarded-fixture extension pl_parse_guarded owned by   */
	/*     section (6) and the established pl_unit/pl_port/pl_drive/       */
	/*     pl_name_span helpers; the step-(a) identity and step-(b)        */
	/*     stage-law owners read only the pipeline node and its body, so   */
	/*     the guarded pipeline drives the unchanged 5-arg seam.           */
	/* ------------------------------------------------------------------ */
	SECTION("compound-guard-published") {
		/* Anchor: a legal 2-stage pipeline under `if (en && f)` lowers and
		 * its ready equation is the complete compound guard: a binary
		 * logical-AND whose reachable objects spell both `en` and `f`. */
		{
			parsed p = pl_parse_guarded("cgp_ok.pigen",
				"module cgp_ok(input logic clk, input logic en, "
				"input logic f, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tif (en && f) begin\n"
				"\t\t\tpipeline cgp_pipe begin\n"
				"\t\t\t\tbit [7:0] a;\n"
				"\t\t\t\tbit [7:0] b;\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\ta <= x;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\tb <= a;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tyield b;\n"
				"\t\t\tendpipeline\n"
				"\t\tend\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			int ready_and = 0;
			int has_en = 0;
			int has_f = 0;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			/* The guarded-fixture extension: the pipeline sits under its
			 * PIGEN_SYNTAX_IF_STATEMENT ancestor. The if's condition is the
			 * compound guard: a binary logical-AND expression whose two
			 * name children resolve, through the expanded-source token
			 * origins (exactly as pl_name_span resolves a name), to the
			 * spans spelling exactly `en` and `f` in source order. This
			 * pins the fixture and both guard spans the ready assertion
			 * checks for. */
			REQUIRE(p.if_statement &&
				p.if_statement->kind == PIGEN_SYNTAX_IF_STATEMENT);
			REQUIRE(!IS_INVALID_ID(p.if_condition));
			{
				const pigen_syntax_expr *cond = pigen_syntax_expr_get(
					&p.tree.expressions, p.if_condition);
				const pigen_syntax_expr *left;
				const pigen_syntax_expr *right;
				pigen_source_span lspan;
				pigen_source_span rspan;

				REQUIRE(cond && cond->kind == PIGEN_SYNTAX_EXPR_BINARY);
				REQUIRE(cond->as.binary.operator == PIGEN_SYNTAX_OP_LOGICAL_AND);
				left = pigen_syntax_expr_get(&p.tree.expressions,
					cond->as.binary.left);
				right = pigen_syntax_expr_get(&p.tree.expressions,
					cond->as.binary.right);
				REQUIRE(left && left->kind == PIGEN_SYNTAX_EXPR_NAME);
				REQUIRE(right && right->kind == PIGEN_SYNTAX_EXPR_NAME);
				lspan = pl_name_span(&p, cond->as.binary.left);
				rspan = pl_name_span(&p, cond->as.binary.right);
				REQUIRE(span_is(&p.sources, lspan, "en"));
				REQUIRE(span_is(&p.sources, rspan, "f"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "en", 1);
			pl_port(&p, &u, "f", 1);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* The pipeline's ready equation is a published RTL equation
			 * whose value is a 1-bit expression. Locate the one that IS a
			 * binary logical-AND: the ready gated by the complete
			 * compound enclosing procedural guard. Against the current
			 * master lowering the ready value is the integer constant 1,
			 * so no such equation exists and ready_and stays 0 (the red). */
			for (i = 0; i < rtl.equation_count && !ready_and; i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;
				const pigen_rtl_type *vt = v ? pigen_rtl_type_get(&rtl,
					v->type) : NULL;

				if (!v || !vt || vt->width != 1)
					continue;
				if (v->kind != PIGEN_RTL_EXPR_BINARY ||
					v->as.binary.resolution.operation.operator !=
						PIGEN_BINARY_LOGICAL_AND)
					continue;
				ready_and = 1;
				/* The complete guard, not a single atom: the ready's
				 * reachable object children must include objects whose
				 * origin spans spell exactly `en` AND exactly `f` in the
				 * parsed source (owner-text check against the span, never
				 * a search over the output). A ready published as one
				 * atom alone, or as the AND of one atom with the
				 * constant 1, fails one of these two pins. */
				has_en = pl_subtree_spells(&rtl, v, &p, "en");
				has_f = pl_subtree_spells(&rtl, v, &p, "f");
			}
			REQUIRE(ready_and);
			REQUIRE(has_en);
			REQUIRE(has_f);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) ready-guarded-by-en: the pipeline's ready equation is gated by  */
	/*     the complete enclosing procedural guard (how/to/reset/and/     */
	/*     enable/a/pipeline.md; include/pigen/pipeline_lower.h guard law) */
	/*     rather than by the constant-1 stand-in the current lowering     */
	/*     publishes (src/pipeline_lower.c: guard = one, ready = guard).   */
	/*     The anchor is the guarded fixture of the accepted pipeline      */
	/*     halting/stop scenario list (what/are/the/pipeline/halt/stop/   */
	/*     test/scenarios.md line 1, reviewed at what/is/the/review/of/   */
	/*     the/pipeline/halt/stop/test/scenarios.md): a legal 2-stage      */
	/*     pipeline nested under `if (en)` inside the clocked process      */
	/*     (1-bit module input en, stage 1 a <= x, stage 2 b <= a, yield  */
	/*     b). It lowers to rc == 0 and the pipeline's ready equation      */
	/*     publishes a 1-bit value that is NOT an integer constant         */
	/*     (PIGEN_RTL_EXPR_INTEGER) and that IS, or contains in its        */
	/*     reachable expression subtree, a PIGEN_RTL_EXPR_OBJECT whose     */
	/*     origin span spells exactly `en` in the parsed source.           */
	/*     Against the current master lowering the ready equation's value  */
	/*     is the integer constant 1, so no published 1-bit handshake      */
	/*     equation carries the guard object and this section is the       */
	/*     discriminating red: it FAILS here and turns green only when the */
	/*     guard-replacement implementation (the later owner, plan item 8) */
	/*     publishes the ready gated by the enclosing guard's expression.  */
	/*     The guard-signal name span is pinned through the expanded-      */
	/*     source token origins (exactly as pl_name_span resolves the if   */
	/*     condition's name), so the assertion is against the parsed       */
	/*     source, never a search over the output. The guarded-fixture     */
	/*     extension (pl_parse_guarded) is the file-local seam the sibling */
	/*     lines 2, 4, 5, 6 reuse; the step-(a) identity and step-(b)      */
	/*     stage-law owners read only the pipeline node and its body, so   */
	/*     the guarded pipeline drives the unchanged 5-arg seam.           */
	/* ------------------------------------------------------------------ */
	SECTION("ready-guarded-by-en") {
		/* Anchor: a legal 2-stage pipeline under `if (en)` lowers and its
		 * ready equation is gated by the guard signal, not the constant-1
		 * stand-in. */
		{
			parsed p = pl_parse_guarded("gre_ok.pigen",
				"module gre_ok(input logic clk, input logic en, "
				"input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tif (en) begin\n"
				"\t\t\tpipeline gre_pipe begin\n"
				"\t\t\t\tbit [7:0] a;\n"
				"\t\t\t\tbit [7:0] b;\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\ta <= x;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\tb <= a;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tyield b;\n"
				"\t\t\tendpipeline\n"
				"\t\tend\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			int found = 0;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			/* The guarded-fixture extension: the pipeline sits under its
			 * PIGEN_SYNTAX_IF_STATEMENT ancestor and the if's condition
			 * resolves, through the expanded-source token origins, to a
			 * name spelling exactly `en` (the guard signal). This pins the
			 * fixture and the guard span the ready assertion checks for. */
			REQUIRE(p.if_statement &&
				p.if_statement->kind == PIGEN_SYNTAX_IF_STATEMENT);
			REQUIRE(!IS_INVALID_ID(p.if_condition));
			{
				const pigen_syntax_expr *cond = pigen_syntax_expr_get(
					&p.tree.expressions, p.if_condition);
				pigen_source_span gspan;

				REQUIRE(cond && cond->kind == PIGEN_SYNTAX_EXPR_NAME);
				gspan = pl_name_span(&p, p.if_condition);
				REQUIRE(span_is(&p.sources, gspan, "en"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "en", 1);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* The pipeline's ready equation is a published RTL equation
			 * whose value is a 1-bit expression. Locate the one that is NOT
			 * an integer constant and whose reachable expression subtree
			 * carries an object spelling exactly `en`: the ready gated by
			 * the complete enclosing procedural guard. Against the current
			 * master lowering the ready value is the integer constant 1, so
			 * no such equation exists and found stays 0 (the red). */
			for (i = 0; i < rtl.equation_count && !found; i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;
				const pigen_rtl_type *vt = v ? pigen_rtl_type_get(&rtl,
					v->type) : NULL;

				if (!v || !vt || vt->width != 1)
					continue;
				if (v->kind == PIGEN_RTL_EXPR_INTEGER)
					continue;
				found = pl_ready_subtree_guard(&rtl, v, &p);
			}
			REQUIRE(found);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) guarded-storage-holds: the complete enclosing procedural guard */
	/*     enables the pipeline, and a FALSE guard holds ALL storage while */
	/*     disabling both handshakes (how/to/reset/and/enable/a/          */
	/*     pipeline.md; the guard law in include/pigen/pipeline_lower.h:  */
	/*     "a false guard holds every stage's storage and disables both   */
	/*     the input and the output handshake, so no item is accepted or  */
	/*     emitted that cycle"). The anchor is the line-5 scenario of the */
	/*     accepted pipeline halting/stop list (what/are/the/pipeline/    */
	/*     halt/stop/test/scenarios.md, reviewed at what/is/the/review/   */
	/*     of/the/pipeline/halt/stop/test/scenarios.md, list pinned       */
	/*     fe0e0742): a legal 2-stage pipeline nested under `if (en)`     */
	/*     inside the clocked process (1-bit module input en, stage 1     */
	/*     a <= x, stage 2 b <= a, yield b). It lowers to rc == 0 AND     */
	/*     publishes BOTH:                                                */
	/*       - its stage-boundary buf storage: exactly (N-1) = 1 one-     */
	/*         entry primitive instance (the same established count the   */
	/*         step-b-witnesses section pins as >= 1 for this 2-stage     */
	/*         pipeline, here tightened to the exact boundary count the   */
	/*         guard must keep);                                          */
	/*       - its ready equation gated by the guard signal: a published  */
	/*         RTL equation whose 1-bit value is NOT an integer constant  */
	/*         (PIGEN_RTL_EXPR_INTEGER) and whose reachable expression    */
	/*         subtree carries a PIGEN_RTL_EXPR_OBJECT whose origin span  */
	/*         spells exactly `en` (reusing the sibling's                 */
	/*         pl_ready_subtree_guard).                                   */
	/*     The conjunction is one result: the false guard holds all       */
	/*     storage (the buf instance stays published) while disabling the */
	/*     handshake (the ready is gated by the guard, not the stand-in). */
	/*     Against the current master lowering the ready equation's       */
	/*     value is the integer constant 1 (src/pipeline_lower.c: guard = */
	/*     one, ready = guard), so no 1-bit non-integer equation carries  */
	/*     the guard object and this section is the discriminating red:   */
	/*     it FAILS here at the ready assertion and turns green only when */
	/*     the guard-replacement implementation (the later owner, plan    */
	/*     item 8) publishes the ready gated by the enclosing guard while */
	/*     keeping the buf storage. A lowering that DROPS the stage        */
	/*     storage (instance_count != 1) or that publishes an UNGUARDED   */
	/*     ready (no 1-bit non-integer equation carrying the guard) fails */
	/*     this section on the matching half. The guard-signal span is    */
	/*     pinned through the parsed if condition (exactly as             */
	/*     pl_name_span resolves a name), never a search over the         */
	/*     output. This section reuses the file-local guarded-fixture     */
	/*     extension pl_parse_guarded and the pl_ready_subtree_guard      */
	/*     helper owned by section (5), and the established               */
	/*     pl_unit/pl_port/pl_drive/pl_name_span/pl_stage_nodes helpers;  */
	/*     the step-(a) identity and step-(b) stage-law owners read only  */
	/*     the pipeline node and its body, so the guarded pipeline drives */
	/*     the unchanged 5-arg seam. This section does not pin the        */
	/*     storage payload width, backpressure, throughput or reset-      */
	/*     binding; those are separate scoped obligations.                */
	/* ------------------------------------------------------------------ */
	SECTION("guarded-storage-holds") {
		/* Anchor: a legal 2-stage pipeline under `if (en)` lowers to rc == 0
		 * and publishes BOTH the stage-boundary buf storage (exactly N-1 = 1
		 * one-entry primitive instance) AND the ready equation gated by the
		 * guard signal (not the constant-1 stand-in). */
		{
			parsed p = pl_parse_guarded("gsh_ok.pigen",
				"module gsh_ok(input logic clk, input logic en, "
				"input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tif (en) begin\n"
				"\t\t\tpipeline gsh_pipe begin\n"
				"\t\t\t\tbit [7:0] a;\n"
				"\t\t\t\tbit [7:0] b;\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\ta <= x;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\tb <= a;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tyield b;\n"
				"\t\t\tendpipeline\n"
				"\t\tend\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			size_t stage_count;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			int storage_ok = 0;
			int ready_ok = 0;

			stage_count = pl_stage_nodes(&p, stages);
			REQUIRE(stage_count == 2);
			/* The guarded-fixture extension: the pipeline sits under its
			 * PIGEN_SYNTAX_IF_STATEMENT ancestor and the if's condition
			 * resolves, through the expanded-source token origins, to a name
			 * spelling exactly `en` (the guard signal). This pins the fixture
			 * and the guard span the ready assertion checks for. */
			REQUIRE(p.if_statement &&
				p.if_statement->kind == PIGEN_SYNTAX_IF_STATEMENT);
			REQUIRE(!IS_INVALID_ID(p.if_condition));
			{
				const pigen_syntax_expr *cond = pigen_syntax_expr_get(
					&p.tree.expressions, p.if_condition);
				pigen_source_span gspan;

				REQUIRE(cond && cond->kind == PIGEN_SYNTAX_EXPR_NAME);
				gspan = pl_name_span(&p, p.if_condition);
				REQUIRE(span_is(&p.sources, gspan, "en"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "en", 1);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* (storage half) the false guard holds all storage: the 2-stage
			 * pipeline's stage-boundary buf storage is exactly (N-1) = 1 one-
			 * entry primitive instance, published alongside the gated ready.
			 * A lowering that drops the stage storage fails here (the same
			 * count the step-b-witnesses section pins as >= 1, tightened to
			 * the exact boundary count). */
			storage_ok = (rtl.instance_count == 1);
			/* (ready half) the handshake is disabled by the guard: the
			 * pipeline's ready equation is a published RTL equation whose
			 * value is a 1-bit expression that is NOT an integer constant and
			 * whose reachable expression subtree carries an object spelling
			 * exactly `en`. Against the current master lowering the ready
			 * value is the integer constant 1, so no such equation exists and
			 * ready_ok stays 0 (the discriminating red). */
			for (i = 0; i < rtl.equation_count && !ready_ok; i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;
				const pigen_rtl_type *vt = v ? pigen_rtl_type_get(&rtl,
					v->type) : NULL;

				if (!v || !vt || vt->width != 1)
					continue;
				if (v->kind == PIGEN_RTL_EXPR_INTEGER)
					continue;
				ready_ok = pl_ready_subtree_guard(&rtl, v, &p);
			}
			/* The conjunction: storage held AND ready gated by the guard. */
			REQUIRE(storage_ok);
			REQUIRE(ready_ok);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) ready-guarded-by-en: the pipeline's ready equation is gated by  */
	/*     the complete enclosing procedural guard (how/to/reset/and/     */
	/*     enable/a/pipeline.md; include/pigen/pipeline_lower.h guard law) */
	/*     rather than by the constant-1 stand-in the current lowering     */
	/*     publishes (src/pipeline_lower.c: guard = one, ready = guard).   */
	/*     The anchor is the guarded fixture of the accepted pipeline      */
	/*     halting/stop scenario list (what/are/the/pipeline/halt/stop/   */
	/*     test/scenarios.md line 1, reviewed at what/is/the/review/of/   */
	/*     the/pipeline/halt/stop/test/scenarios.md): a legal 2-stage      */
	/*     pipeline nested under `if (en)` inside the clocked process      */
	/*     (1-bit module input en, stage 1 a <= x, stage 2 b <= a, yield  */
	/*     b). It lowers to rc == 0 and the pipeline's ready equation      */
	/*     publishes a 1-bit value that is NOT an integer constant         */
	/*     (PIGEN_RTL_EXPR_INTEGER) and that IS, or contains in its        */
	/*     reachable expression subtree, a PIGEN_RTL_EXPR_OBJECT whose     */
	/*     origin span spells exactly `en` in the parsed source.           */
	/*     Against the current master lowering the ready equation's value  */
	/*     is the integer constant 1, so no published 1-bit handshake      */
	/*     equation carries the guard object and this section is the       */
	/*     discriminating red: it FAILS here and turns green only when the */
	/*     guard-replacement implementation (the later owner, plan item 8) */
	/*     publishes the ready gated by the enclosing guard's expression.  */
	/*     The guard-signal name span is pinned through the expanded-      */
	/*     source token origins (exactly as pl_name_span resolves the if   */
	/*     condition's name), so the assertion is against the parsed       */
	/*     source, never a search over the output. The guarded-fixture     */
	/*     extension (pl_parse_guarded) is the file-local seam the sibling */
	/*     lines 2, 4, 5, 6 reuse; the step-(a) identity and step-(b)      */
	/*     stage-law owners read only the pipeline node and its body, so   */
	/*     the guarded pipeline drives the unchanged 5-arg seam.           */
	/* ------------------------------------------------------------------ */
	SECTION("ready-guarded-by-en") {
		/* Anchor: a legal 2-stage pipeline under `if (en)` lowers and its
		 * ready equation is gated by the guard signal, not the constant-1
		 * stand-in. */
		{
			parsed p = pl_parse_guarded("gre_ok.pigen",
				"module gre_ok(input logic clk, input logic en, "
				"input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tif (en) begin\n"
				"\t\t\tpipeline gre_pipe begin\n"
				"\t\t\t\tbit [7:0] a;\n"
				"\t\t\t\tbit [7:0] b;\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\ta <= x;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\tb <= a;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tyield b;\n"
				"\t\t\tendpipeline\n"
				"\t\tend\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			int found = 0;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			/* The guarded-fixture extension: the pipeline sits under its
			 * PIGEN_SYNTAX_IF_STATEMENT ancestor and the if's condition
			 * resolves, through the expanded-source token origins, to a
			 * name spelling exactly `en` (the guard signal). This pins the
			 * fixture and the guard span the ready assertion checks for. */
			REQUIRE(p.if_statement &&
				p.if_statement->kind == PIGEN_SYNTAX_IF_STATEMENT);
			REQUIRE(!IS_INVALID_ID(p.if_condition));
			{
				const pigen_syntax_expr *cond = pigen_syntax_expr_get(
					&p.tree.expressions, p.if_condition);
				pigen_source_span gspan;

				REQUIRE(cond && cond->kind == PIGEN_SYNTAX_EXPR_NAME);
				gspan = pl_name_span(&p, p.if_condition);
				REQUIRE(span_is(&p.sources, gspan, "en"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "en", 1);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* The pipeline's ready equation is a published RTL equation
			 * whose value is a 1-bit expression. Locate the one that is NOT
			 * an integer constant and whose reachable expression subtree
			 * carries an object spelling exactly `en`: the ready gated by
			 * the complete enclosing procedural guard. Against the current
			 * master lowering the ready value is the integer constant 1, so
			 * no such equation exists and found stays 0 (the red). */
			for (i = 0; i < rtl.equation_count && !found; i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;
				const pigen_rtl_type *vt = v ? pigen_rtl_type_get(&rtl,
					v->type) : NULL;

				if (!v || !vt || vt->width != 1)
					continue;
				if (v->kind == PIGEN_RTL_EXPR_INTEGER)
					continue;
				found = pl_ready_subtree_guard(&rtl, v, &p);
			}
			REQUIRE(found);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (7) ready-renders-guard-sv: the guarded ready renders the guard     */
	/*     signal through the terminal SV (scenario line 6 of the          */
	/*     accepted pipeline halting/stop list, what/are/the/pipeline/     */
	/*     halt/stop/test/scenarios.md, independently reviewed and         */
	/*     accepted at what/is/the/review/of/the/pipeline/halt/stop/      */
	/*     test/scenarios.md, list pinned fe0e0742). The anchor is the     */
	/*     guarded pipeline of scenario 1: a legal 2-stage pipeline        */
	/*     nested under `if (en)` inside the clocked process (1-bit module */
	/*     input en, stage 1 a <= x, stage 2 b <= a, yield b). It lowers   */
	/*     to rc == 0 and the ready equation's value renders through       */
	/*     pigen_sv_render_expression (after pigen_rtl_assign_names,       */
	/*     PIGEN_RTL_NAME_SOURCE, for the reachable object origins) as     */
	/*     text containing the spelled guard signal `en` and not the bare  */
	/*     constant `1` - the emitted handshake is gated by the guard's    */
	/*     spelling, not the stand-in (how/to/reset/and/enable/a/          */
	/*     pipeline.md guard law; include/pigen/pipeline_lower.h). The     */
	/*     ready equation is located among the published 1-bit RTL         */
	/*     equations by its guard-gated value: against the current master  */
	/*     lowering that value is the integer constant 1 (src/             */
	/*     pipeline_lower.c: guard = one, ready = guard), so the rendered  */
	/*     text is the bare constant `1` and this section is the           */
	/*     discriminating red: it FAILS here at the rendered-text          */
	/*     assertion and turns green only when the guard-replacement       */
	/*     implementation (the later owner, plan item 8) publishes the     */
	/*     ready gated by the enclosing guard's expression, whose object   */
	/*     renders through its SOURCE name as `en`. The guard-signal name  */
	/*     span is pinned through the expanded-source token origins        */
	/*     (exactly as pl_name_span resolves the if condition's name), so  */
	/*     the fixture pin is against the parsed source, never a search    */
	/*     over the output. The name assignment and the render follow the  */
	/*     established emitted-member-order pattern (the reachable         */
	/*     OBJECT origins named PIGEN_RTL_NAME_SOURCE, then rendered).     */
	/*     The guarded-fixture extension (pl_parse_guarded) is the         */
	/*     file-local seam the sibling lines 1, 2, 4, 5 share; the         */
	/*     step-(a) identity and step-(b) stage-law owners read only the   */
	/*     pipeline node and its body, so the guarded pipeline drives the  */
	/*     unchanged 5-arg seam.                                           */
	/* ------------------------------------------------------------------ */
	SECTION("ready-renders-guard-sv") {
		/* Anchor: a legal 2-stage pipeline under `if (en)` lowers to rc == 0
		 * and the ready equation's value renders through the terminal SV as
		 * text containing the spelled guard signal `en` and not the bare
		 * constant `1`. */
		{
			parsed p = pl_parse_guarded("grgs_ok.pigen",
				"module grgs_ok(input logic clk, input logic en, "
				"input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tif (en) begin\n"
				"\t\t\tpipeline grgs_pipe begin\n"
				"\t\t\t\tbit [7:0] a;\n"
				"\t\t\t\tbit [7:0] b;\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\ta <= x;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tstage begin\n"
				"\t\t\t\t\tb <= a;\n"
				"\t\t\t\tend\n"
				"\t\t\t\tyield b;\n"
				"\t\t\tendpipeline\n"
				"\t\tend\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			pigen_rtl_expr_id ready_value =
				(pigen_rtl_expr_id){PIGEN_INVALID_ID};

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			/* The guarded-fixture extension: the pipeline sits under its
			 * PIGEN_SYNTAX_IF_STATEMENT ancestor and the if's condition
			 * resolves, through the expanded-source token origins, to a
			 * name spelling exactly `en` (the guard signal). */
			REQUIRE(p.if_statement &&
				p.if_statement->kind == PIGEN_SYNTAX_IF_STATEMENT);
			REQUIRE(!IS_INVALID_ID(p.if_condition));
			{
				const pigen_syntax_expr *cond = pigen_syntax_expr_get(
					&p.tree.expressions, p.if_condition);
				pigen_source_span gspan;

				REQUIRE(cond && cond->kind == PIGEN_SYNTAX_EXPR_NAME);
				gspan = pl_name_span(&p, p.if_condition);
				REQUIRE(span_is(&p.sources, gspan, "en"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "en", 1);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* Locate the ready equation among the published 1-bit RTL
			 * equations: the guard-gated ready is the 1-bit equation whose
			 * value is not the constant-0 stand-in the per-stage
			 * next-value equations carry (the next-value equations are
			 * width total_bits and constant 0; the ready is 1-bit). Its
			 * value is the guard - against the current master lowering the
			 * integer constant 1, so the rendered text below is the bare
			 * constant `1` (the discriminating red); once the guard is
			 * published as its expression the value renders as the guard's
			 * spelled signal. */
			for (i = 0; i < rtl.equation_count && IS_INVALID_ID(ready_value);
				i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;
				const pigen_rtl_type *vt = v ? pigen_rtl_type_get(&rtl,
					v->type) : NULL;

				if (!v || !vt || vt->width != 1)
					continue;
				if (v->kind == PIGEN_RTL_EXPR_INTEGER && v->value == 0)
					continue;
				ready_value = eq->value;
			}
			REQUIRE(!IS_INVALID_ID(ready_value));
			/* Name the ready value's reachable OBJECT origins (SOURCE role)
			 * so the terminal renderer can spell them, then render the ready
			 * value through the terminal SV emission. */
			{
				pigen_rtl_name_request requests[8];
				size_t request_count = 0;
				char *rendered = (char *)"sentinel";

				for (i = 0; i < rtl.expression_count; i++)
				{
					const pigen_rtl_expr *e = pigen_rtl_expr_get(&rtl,
						(pigen_rtl_expr_id){(uint32_t)i});

					if (!e || e->kind != PIGEN_RTL_EXPR_OBJECT)
						continue;
					requests[request_count].kind = PIGEN_RTL_NAME_SOURCE;
					requests[request_count].origin =
						pigen_rtl_object_get(&rtl, e->object)->origin;
					request_count++;
					REQUIRE(request_count <= 8);
				}
				if (request_count)
					REQUIRE(pigen_rtl_assign_names(&rtl, &p.sources, requests,
						request_count).index != PIGEN_INVALID_ID);
				/* Render the ready value through the terminal SV emission and
				 * pin the emitted handshake: the text contains the spelled
				 * guard signal `en` and is not the bare constant `1`. Against
				 * the current master lowering the ready value is the integer
				 * constant 1, so the rendered text is "1" and this fails -
				 * the emitted handshake is the stand-in, not the guard. */
				REQUIRE(pigen_sv_render_expression(&rtl,
					pigen_rtl_expr_get(&rtl, ready_value), &rendered) == 0);
				REQUIRE(rendered != NULL && rendered != (char *)"sentinel");
				REQUIRE(strstr(rendered, "en") != NULL);
				REQUIRE(strstr(rendered, "1") == NULL);
				free(rendered);
			}
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (8) calc-arithmetic: a stage performs its specified next-value      */
	/*     computation over the immutable incoming fields (how/do/        */
	/*     pipeline/stage/reads/work.md; what/is/the/pipeline/parent/     */
	/*     module/lowering.md step (c)): the published stage next-value   */
	/*     is the stage's expression over its inputs, NOT an object       */
	/*     pass-through and NOT a constant. The anchor is line 1 of the   */
	/*     accepted pipeline stage computation scenario list (what/are/   */
	/*     the/pipeline/stage/computation/test/scenarios.md, reviewed     */
	/*     at what/is/the/review/of/the/pipeline/stage/computation/test/ */
	/*     scenarios.md, list pinned c3513cb5): a legal 2-stage pipeline  */
	/*     (stage 1 `a <= x`, stage 2 `b <= a + 8'h3D`, `yield b`) whose  */
	/*     stage 2 computes over the immutable incoming 8-bit field `a`.  */
	/*     It lowers to rc == 0 and publishes, for stage 2, a             */
	/*     combinational next-value equation whose value is a binary `+`  */
	/*     expression (PIGEN_RTL_EXPR_BINARY, PIGEN_BINARY_ADD) with      */
	/*     EXACTLY two children: one a PIGEN_RTL_EXPR_OBJECT whose        */
	/*     object (located through pigen_rtl_object_get) is the 8-bit     */
	/*     incoming field `a` - its origin span spelling exactly `a` in   */
	/*     the parsed source - and the other the integer 8'h3D (a         */
	/*     PIGEN_RTL_EXPR_INTEGER of width 8 with value 0x3D = 61). The   */
	/*     two-object shape pins that the value is a binary + over the    */
	/*     incoming field AND the literal: an object pass-through (a      */
	/*     single object child, or a wider binary naming no second        */
	/*     operand) and a constant stand-in both fail the pins. The      */
	/*     fixture is pinned through the parse: stage 2's single          */
	/*     nonblocking-assignment item carries a binary `+` expression    */
	/*     whose left name spells exactly `a` and whose right literal     */
	/*     spells exactly `8'h3D`, so the assertion is against the        */
	/*     parsed source, never a search over the output. This section is */
	/*     the discriminating red: against the current master lowering    */
	/*     (src/pipeline_lower.c: the per-stage next-value equation       */
	/*     value is the constant-0 integer stand-in) no published         */
	/*     equation carries a binary `+` value, so the location finds     */
	/*     nothing and this section FAILS here; it turns green when the   */
	/*     stage-computation implementation publishes stage 2's next      */
	/*     value as the binary + over the incoming field object and the   */
	/*     literal. The same unguarded 2-stage fixture shape the other    */
	/*     sections use, reached through the established seam helpers     */
	/*     (pl_parse/pl_unit/pl_port/pl_stage_nodes/pl_drive) unchanged.  */
	/* ------------------------------------------------------------------ */
	SECTION("calc-arithmetic") {
		/* Anchor: a legal 2-stage pipeline whose stage 2 computes
		 * `b <= a + 8'h3D` over the immutable incoming 8-bit field `a`
		 * lowers to rc == 0 and publishes stage 2's next-value equation
		 * as a binary `+` over the field's object and the literal. */
		{
			parsed p = pl_parse("ca_ok.pigen",
				"module ca_ok(input logic clk, input logic [7:0] x);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline ca_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tb <= a + 8'h3D;\n"
				"\t\t\tend\n"
				"\t\t\tyield b;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *items[8];
			size_t item_count;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			int found = 0;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			/* The fixture pin through the parse: stage 2's single item is a
			 * nonblocking assignment whose value is a binary `+` expression
			 * over the name `a` (the incoming field, resolved through the
			 * expanded-source token origins) and the literal `8'h3D` - the
			 * spans the assertion below checks for. */
			item_count = pl_stage_items(&p, stages[1], items);
			REQUIRE(item_count == 1);
			REQUIRE(items[0]->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			{
				const pigen_syntax_expr *rhs = pigen_syntax_expr_get(
					&p.tree.expressions,
					items[0]->as.nonblocking_assignment.value);
				const pigen_syntax_expr *left;
				const pigen_syntax_expr *right;
				const pigen_expanded_token *token;
				pigen_source_span lspan;
				pigen_source_span rspan;

				REQUIRE(rhs && rhs->kind == PIGEN_SYNTAX_EXPR_BINARY);
				REQUIRE(rhs->as.binary.operator == PIGEN_SYNTAX_OP_ADD);
				left = pigen_syntax_expr_get(&p.tree.expressions,
					rhs->as.binary.left);
				right = pigen_syntax_expr_get(&p.tree.expressions,
					rhs->as.binary.right);
				REQUIRE(left && left->kind == PIGEN_SYNTAX_EXPR_NAME);
				lspan = pl_name_span(&p, rhs->as.binary.left);
				REQUIRE(span_is(&p.sources, lspan, "a"));
				REQUIRE(right && right->kind == PIGEN_SYNTAX_EXPR_LITERAL);
				token = pigen_expanded_token_get(&p.pre.expanded,
					right->as.atom.token);
				REQUIRE(token);
				rspan = pigen_origin_spelling_span(&p.pre.expanded,
					token->origin);
				REQUIRE(span_is(&p.sources, rspan, "8'h3D"));
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* Locate stage 2's next-value equation among the published RTL
			 * equations: the one whose value is a binary `+`
			 * (PIGEN_RTL_EXPR_BINARY, PIGEN_BINARY_ADD). Against the current
			 * master lowering the per-stage next-value values are the
			 * constant-0 integer stand-in and no published equation carries
			 * a binary + value, so found stays 0 - the discriminating red. */
			for (i = 0; i < rtl.equation_count && !found; i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;

				if (!v || v->kind != PIGEN_RTL_EXPR_BINARY)
					continue;
				if (v->as.binary.resolution.operation.operator !=
					PIGEN_BINARY_ADD)
					continue;
				found = 1;
				/* The value is a binary + over EXACTLY two operands: an
				 * object pass-through (one object, no second operand) or a
				 * wider expression fails the child count; a constant
				 * stand-in failed the kind above. */
				{
					const pigen_rtl_expr_id *children;
					size_t child_count;

					children = pigen_rtl_expr_children(&rtl, eq->value,
						&child_count);
					REQUIRE(children && child_count == 2);
				}
				/* One operand is the incoming field `a`'s object: a
				 * PIGEN_RTL_EXPR_OBJECT whose object (located through
				 * pigen_rtl_object_get) carries the 8-bit type and whose
				 * origin span spells exactly `a` in the parsed source. The
				 * other is the integer 8'h3D: a PIGEN_RTL_EXPR_INTEGER of
				 * width 8 with value 0x3D. A value naming the enclosing
				 * input `x`, the outgoing field `b`, or the literal alone
				 * fails the matching pin. */
				{
					int field_a = 0;
					int literal = 0;
					const pigen_rtl_expr_id *children;
					size_t child_count;
					size_t c;

					children = pigen_rtl_expr_children(&rtl, eq->value,
						&child_count);
					for (c = 0; c < child_count; c++)
					{
						const pigen_rtl_expr *ch = pigen_rtl_expr_get(&rtl,
							children[c]);
						const pigen_rtl_type *t;

						if (!ch)
							continue;
						t = pigen_rtl_type_get(&rtl, ch->type);
						if (ch->kind == PIGEN_RTL_EXPR_OBJECT && t &&
							t->width == 8)
						{
							const pigen_rtl_object *obj =
								pigen_rtl_object_get(&rtl, ch->object);

							if (obj && span_is(&p.sources,
								obj->origin, "a"))
								field_a = 1;
						}
						if (ch->kind == PIGEN_RTL_EXPR_INTEGER && t &&
							t->width == 8 && ch->value == 0x3D)
							literal = 1;
					}
					REQUIRE(field_a);
					REQUIRE(literal);
				}
			}
			REQUIRE(found);
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* calc-bitwise: a pipeline stage performs its specified nontrivial    */
	/* next-value computation on concrete input (what/are/the/pipeline/    */
	/* stage/computation/test/scenarios.md line 2). The anchor is a legal   */
	/* 2-stage pipeline whose stage 1 writes the two 8-bit incoming fields */
	/* a and b from external inputs x and y, and whose stage 2 computes     */
	/* `c <= a & b` over those two immutable incoming fields. The          */
	/* repartition law (how/to/repartition/pipeline/boundaries.md) is the   */
	/* seam's own full-contract obligation: a later stage's incoming and    */
	/* outgoing total $bits must be equal, so stage 2's 16-bit incoming     */
	/* packet (a + b) is written into the 16-bit field c (the 8-bit `a & b` */
	/* expression zero-extends into it); an 8-bit c would be an unequal     */
	/* boundary the seam rejects (rc == -1). The anchor lowers to rc == 0   */
	/* and pins stage 2's published next-value equation: its value is a     */
	/* binary bitwise-and (PIGEN_RTL_EXPR_BINARY, operator                 */
	/* PIGEN_BINARY_BITWISE_AND) over TWO PIGEN_RTL_EXPR_OBJECT operands    */
	/* whose provenance objects name BOTH incoming field objects - the      */
	/* 8-bit field a and the 8-bit field b - not an object pass-through and */
	/* not the integer-0 stand-in the current master publishes. The gate    */
	/* the section discriminates is that the lowering publishes the stage's */
	/* COMPUTED expression, not a placeholder: against the current master    */
	/* (src/pipeline_lower.c's constant-0 stand-in) no published equation   */
	/* carries a binary bitwise-and, so the search below finds none and the */
	/* section is red; once the stand-in is replaced with the stage's       */
	/* published expression the search locates stage 2's equation and the   */
	/* operand checks pass. A permissive builder that keeps the stand-in,   */
	/* or that publishes a non-and binary (or a single-operand, or an       */
	/* integer) value for stage 2, fails the section.                       */
	/* ------------------------------------------------------------------ */
	SECTION("calc-bitwise") {
		/* Anchor: a legal 2-stage pipeline whose stage 2 computes `c <= a &
		 * b` over the two 8-bit incoming fields a and b (stage 1 writes
		 * both from external inputs x and y) lowers to rc == 0 and publishes
		 * stage 2's next-value equation as a binary bitwise-and over both
		 * incoming field objects. The repartition law keeps the boundary
		 * legal: stage 2's 16-bit incoming (a + b) is written into the 16-
		 * bit field c, so the carried packet neither widens nor narrows. */
		{
			parsed p = pl_parse("cbw_ok.pigen",
				"module cbw_ok(input logic clk, input logic [7:0] x, "
				"input logic [7:0] y);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline cbw_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tbit [15:0] c;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= x;\n"
				"\t\t\t\tb <= y;\n"
				"\t\t\tend\n"
				"\t\t\tstage begin\n"
				"\t\t\t\tc <= a & b;\n"
				"\t\t\tend\n"
				"\t\t\tyield c;\n"
				"\t\tendpipeline\n"
				"\tend\n"
				"endmodule");
			unit u;
			const pigen_syntax_node *stages[4];
			const pigen_syntax_node *fields[8];
			size_t stage_count;
			size_t field_count;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			pigen_rtl_expr_id and_value =
				(pigen_rtl_expr_id){PIGEN_INVALID_ID};

			stage_count = pl_stage_nodes(&p, stages);
			REQUIRE(stage_count == 2);
			field_count = pl_field_nodes(&p, fields);
			REQUIRE(field_count == 3);
			/* Field widths from the parsed declaration spans: a = 8, b = 8,
			 * c = 16. The repartition law keeps the boundary equal (16 in,
			 * 16 out); an 8-bit c would be the unequal boundary the seam
			 * rejects, so the legal fixture writes the 16-bit field. */
			REQUIRE(pl_field_width(&p, fields[0]) == 8);
			REQUIRE(pl_field_width(&p, fields[1]) == 8);
			REQUIRE(pl_field_width(&p, fields[2]) == 16);
			pl_unit(&p, &u);
			pl_port(&p, &u, "x", 8);
			pl_port(&p, &u, "y", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* Locate stage 2's next-value equation: the published equation
			 * whose value is a binary bitwise-and (PIGEN_RTL_EXPR_BINARY,
			 * operator PIGEN_BINARY_BITWISE_AND). Against the current master
			 * (the constant-0 stand-in) no such equation is published, so the
			 * search finds none and the section is red. */
			for (i = 0; i < rtl.equation_count && IS_INVALID_ID(and_value); i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;

				if (v && v->kind == PIGEN_RTL_EXPR_BINARY &&
					v->as.binary.resolution.operation.operator ==
					PIGEN_BINARY_BITWISE_AND)
					and_value = eq->value;
			}
			REQUIRE(!IS_INVALID_ID(and_value));
			/* The bitwise-and's two operands are the two incoming field
			 * objects: exactly two children, each a PIGEN_RTL_EXPR_OBJECT,
			 * whose provenance objects name the field a and the field b (an
			 * owner-text check against each object's origin span, never a
			 * source-text search over the output). Both fields must be
			 * named - one operand, or a third, fails. */
			{
				const pigen_rtl_expr *and_expr =
					pigen_rtl_expr_get(&rtl, and_value);
				const pigen_rtl_expr_id *children;
				size_t child_count = 0;
				int named_a = 0;
				int named_b = 0;

				REQUIRE(and_expr->kind == PIGEN_RTL_EXPR_BINARY);
				children = pigen_rtl_expr_children(&rtl, and_value,
					&child_count);
				REQUIRE(children && child_count == 2);
				for (i = 0; i < child_count; i++)
				{
					const pigen_rtl_expr *c = pigen_rtl_expr_get(&rtl,
						children[i]);
					const pigen_rtl_object *obj;

					REQUIRE(c && c->kind == PIGEN_RTL_EXPR_OBJECT);
					obj = pigen_rtl_object_get(&rtl, c->object);
					REQUIRE(obj);
					if (span_is(&p.sources, obj->origin, "a"))
						named_a = 1;
					else if (span_is(&p.sources, obj->origin, "b"))
						named_b = 1;
					else
						REQUIRE(0); /* an operand that names neither
							incoming field */
				}
				/* BOTH incoming field objects are named, exactly once each. */
				REQUIRE(named_a == 1);
				REQUIRE(named_b == 1);
			}
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (6) calc-first-stage: a first-stage computation over an enclosing     */
	/*     signal. A legal 2-stage pipeline whose first stage computes       */
	/*     `a <= d + 8'h01` over the enclosing module 8-bit input d (no      */
	/*     pipeline-field read in stage 1, a consuming external read that is */
	/*     legal under the step-(b) first-stage law) lowers to rc == 0 and   */
	/*     publishes stage 1's next-value equation whose value is the        */
	/*     computed expression referencing the enclosing signal d's object - */
	/*     not the constant-0 integer stand-in the seam currently publishes, */
	/*     and not a plain object pass-through (what/are/the/pipeline/stage/ */
	/*     computation/test/scenarios.md, line 3; the reference is pinned    */
	/*     through the object's provenance name span spelling d, the owner   */
	/*     text check, never a source-text search over the output).          */
	/* ------------------------------------------------------------------ */
	SECTION("calc-first-stage") {
		/* Anchor: a legal 2-stage pipeline whose first stage computes
		 * `a <= d + 8'h01` over the enclosing 8-bit input d lowers to rc == 0
		 * and publishes stage 1's next-value equation whose value is a
		 * computed expression referencing d's object. */
		{
			parsed p = pl_parse("cfs_ok.pigen",
				"module cfs_ok(input logic clk, input logic [7:0] d);\n"
				"\talways @(posedge clk) begin\n"
				"\t\tpipeline cfs_pipe begin\n"
				"\t\t\tbit [7:0] a;\n"
				"\t\t\tbit [7:0] b;\n"
				"\t\t\tstage begin\n"
				"\t\t\t\ta <= d + 8'h01;\n"
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
			size_t item_count;
			pigen_rtl_lowering lowering;
			pigen_rtl_model rtl;
			int rc;
			size_t i;
			const pigen_rtl_expr *stage1_value;

			REQUIRE(pl_stage_nodes(&p, stages) == 2);
			/* Locate the first stage's one nonblocking-assignment item and
			 * confirm its value is the computed `d + 8'h01`, not a bare name
			 * pass-through: the enclosing input d is read (a consuming
			 * external read, legal in the first stage), combined with the
			 * integer constant 8'h01. */
			item_count = pl_stage_items(&p, stages[0], items);
			REQUIRE(item_count == 1);
			REQUIRE(items[0]->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT);
			{
				const pigen_syntax_expr *value = pigen_syntax_expr_get(
					&p.tree.expressions,
					items[0]->as.nonblocking_assignment.value);

				REQUIRE(value &&
					value->kind != PIGEN_SYNTAX_EXPR_NAME);
			}
			pl_unit(&p, &u);
			pl_port(&p, &u, "d", 8);
			rc = pl_drive(&p, &u, &lowering, &rtl);
			REQUIRE(rc == 0);
			/* Locate stage 1's next-value equation: the first published
			 * equation whose value is not the constant-0 integer stand-in
			 * (the per-stage next-value stand-in the seam currently carries)
			 * and not the yield write's concatenation. The ready equation's
			 * value is the 1-bit guard; the stage next-value values are
			 * packet-width. On the intended lowering stage 1's next value is
			 * the computed `d + 8'h01` - a non-integer, non-object expression
			 * that references d's object in its reachable subtree. */
			stage1_value = NULL;
			for (i = 0; i < rtl.equation_count && !stage1_value; i++)
			{
				const pigen_rtl_equation *eq = pigen_rtl_equation_get(&rtl,
					(pigen_rtl_equation_id){(uint32_t)i});
				const pigen_rtl_expr *v = eq ? pigen_rtl_expr_get(&rtl,
					eq->value) : NULL;

				if (!v)
					continue;
				if (v->kind == PIGEN_RTL_EXPR_CONCATENATION)
					continue; /* the yield write */
				if (v->kind == PIGEN_RTL_EXPR_INTEGER)
					continue; /* the per-stage constant stand-in */
				stage1_value = v;
			}
			REQUIRE(stage1_value);
			/* The discriminator: stage 1's next value is a computed
			 * expression (not a constant stand-in, not a plain object
			 * pass-through) that references the enclosing signal d's object
			 * in its reachable subtree. Against the current master lowering
			 * the stage next values are the constant-0 stand-in, so no
			 * non-integer, non-object next value exists and stage1_value is
			 * NULL - the section is red; the constant stand-in is replaced
			 * by the computed expression and this goes green. */
			REQUIRE(stage1_value->kind != PIGEN_RTL_EXPR_OBJECT);
			REQUIRE(pl_calc_subtree_refs(&rtl, stage1_value, &p, "d"));
			pl_free_unit(&u);
			pigen_free_rtl_model(&rtl);
			pl_free(&p);
		}
	}

	return check_finish();
}
