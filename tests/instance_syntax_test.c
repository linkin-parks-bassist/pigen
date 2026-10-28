/* Structured PIGEN_SYNTAX_INSTANCE syntax-node test contract.
 *
 * (i1) of the child-instance stage: the discriminating green manifest that
 * pins the structured syntax node for one ordinary SV child instantiation
 * against the behavior contract the spec and the brief require (the instance
 * node contract in include/pigen/syntax.h; the recognition shape and fail-loud
 * contract in the task brief; the "ordinary SV child instantiation remains
 * accepted" fact is/struct/like/child/instantiation/specified.md). It parses
 * module-level instance items through pigen_parse_syntax and pins, with
 * original source spans and child counts (never a source-text search over the
 * output), the exact node structure the contract names.
 *
 * Four sections, one `make instance-syntax-test T=<section>` entry per section
 * in tests/contracts.json:
 *
 *   (a) parameter-mixed-connections: a legal instantiation with one parameter
 *       override and mixed named/positional connections parses to a
 *       PIGEN_SYNTAX_INSTANCE carrying the exact type/name tokens, the two
 *       half-open child ranges (one PIGEN_SYNTAX_PARAMETER then the
 *       PIGEN_SYNTAX_INSTANCE_CONNECTION children in source order), the
 *       per-connection port tokens and expression spans, and the whole-item
 *       original span.
 *   (b) empty-params-named-only: a legal instantiation with an empty
 *       parameter list and only named connections parses to a
 *       PIGEN_SYNTAX_INSTANCE with a zero-count parameter range and one
 *       connection child per named port, each pinning its port token and
 *       expression span.
 *   (c) fail-loud-reds: a missing ';', an unterminated connection list, and a
 *       connection that is neither .PORT(expr) nor an expression each report
 *       a non-NULL diagnostic at the offending token's original span and
 *       return 0 (no opaque fallback).
 *   (d) data-first-not-instance: a data-first declaration beginning with an
 *       identifier, an identifier, then '(' does NOT parse as a silent
 *       instance node - it takes the declaration route or a fail-loud
 *       diagnostic at the correct span, never an instance.
 *
 * Every witness is located through the parse (the module's children, the
 * instance node and its parameter/connection children) and every provenance is
 * a span check against the parsed source. A failed parse is a non-NULL
 * diagnostic located at the offending token's original span; a parse that
 * returns 0 without a message, or with the diagnostic off the offending token,
 * is a manifest defect, not a red.
 */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/expression.h"
#include "pigen/preprocess.h"
#include "pigen/source.h"
#include "pigen/syntax.h"

#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)

typedef struct {
	pigen_source_manager sources;
	pigen_source_id source;
	pigen_preprocess_result pre;
	pigen_syntax_tree tree;
	pigen_syntax_error error;
	const pigen_syntax_node *module;
	const pigen_syntax_node *instance;
} parsed;

static int span_is(const pigen_source_manager *srcs, pigen_source_span span,
	const char *word)
{
	size_t length;
	const char *text = pigen_source_span_text(srcs, span, &length);

	return text && length == strlen(word) && !memcmp(text, word, length);
}

/* Pins a node's original span to the exact byte range [start, end) of SOURCE
 * (a dropped or zeroed span, or a wrong source, cannot match). */
static int span_is_range(const pigen_source_manager *srcs, pigen_source_span span,
	pigen_source_id source_id, const char *source, size_t start, size_t end)
{
	size_t length;
	const char *text = pigen_source_span_text(srcs, span, &length);

	return text && span.source.index == source_id.index && span.start == start &&
		span.end == end && end > start &&
		!memcmp(source + start, text, length);
}

static size_t occurrence_start(const char *source, const char *needle,
	size_t occurrence)
{
	const char *at = source;

	REQUIRE(occurrence);
	for (size_t i = 0; i < occurrence; i++)
	{
		at = strstr(at, needle);
		REQUIRE(at);
		if (i + 1 < occurrence) at++;
	}
	return (size_t)(at - source);
}

static int token_is(const pigen_expanded_source *src, pigen_token_id token,
	const char *word)
{
	const pigen_expanded_token *known = pigen_expanded_token_get(src, token);
	const char *text;
	size_t length;

	if (!known) return 0;
	text = pigen_expanded_token_text(src, known, &length);
	return text && length == strlen(word) && !memcmp(text, word, length);
}

static int expr_span_is(const pigen_source_manager *srcs,
	const pigen_syntax_tree *tree, pigen_syntax_expr_id expression,
	const char *word)
{
	const pigen_syntax_expr *known = pigen_syntax_expr_get(&tree->expressions,
		expression);

	return known && span_is(srcs, known->location.source_span, word);
}

static void in_parse(parsed *out, const char *name, const char *text)
{
	const pigen_syntax_node *root;
	const pigen_syntax_node *module = NULL;
	const pigen_syntax_node *instance = NULL;

	memset(out, 0, sizeof *out);
	out->source = pigen_source_add(&out->sources, name, text, strlen(text));
	REQUIRE(!IS_INVALID_ID(out->source));
	{
		pigen_preprocess_error pre_error;

		memset(&pre_error, 0, sizeof pre_error);
		REQUIRE(pigen_preprocess(&out->sources, out->source, NULL, &out->pre,
			&pre_error));
	}
	{
		int ok = pigen_parse_syntax(&out->pre.expanded, &out->tree,
			&out->error);
		root = pigen_syntax_get(&out->tree, (pigen_syntax_id){0});
		REQUIRE(root && root->kind == PIGEN_SYNTAX_COMPILATION_UNIT);
		for (pigen_syntax_id child = root->first_child;
			child.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(&out->tree, child);

			REQUIRE(node);
			if (node->kind == PIGEN_SYNTAX_MODULE) module = node;
			child = node->next_sibling;
		}
		out->module = module;
		REQUIRE(module);
		for (pigen_syntax_id child = module->first_child;
			child.index != PIGEN_INVALID_ID; )
		{
			const pigen_syntax_node *node = pigen_syntax_get(&out->tree, child);

			REQUIRE(node);
			if (node->kind == PIGEN_SYNTAX_INSTANCE) instance = node;
			child = node->next_sibling;
		}
		out->instance = instance;
		if (!ok) return;
	}
}

static void in_free(parsed *p)
{
	pigen_free_syntax_tree(&p->tree);
	pigen_free_preprocess_result(&p->pre);
	pigen_free_sources(&p->sources);
	*p = (parsed){0};
}

int main(int argc, char **argv)
{
	check_init(argc, argv);

	/* ------------------------------------------------------------------ */
	/* (a) parameter-mixed-connections: one parameter override and mixed   */
	/*     named/positional connections parse to the full instance node.   */
	/* ------------------------------------------------------------------ */
	SECTION("parameter-mixed-connections") {
		const char *text =
			"module top(input logic clk, input logic a, input logic b);\n"
			"\tram #(W = 8) r1 (.din(a), b, .clk(clk));\n"
			"endmodule";
		parsed p;
		const pigen_syntax_node *inst;
		const pigen_syntax_node *param;

		in_parse(&p, "a.pigen", text);
		REQUIRE(p.error.message == NULL);
		inst = p.instance;
		REQUIRE(inst && inst->kind == PIGEN_SYNTAX_INSTANCE);
		REQUIRE(token_is(&p.pre.expanded, inst->as.instance.type_name, "ram"));
		REQUIRE(token_is(&p.pre.expanded, inst->as.instance.name, "r1"));
		REQUIRE(inst->as.instance.parameter_count == 1);
		REQUIRE(inst->as.instance.connection_count == 3);

		/* The single parameter child is the one PIGEN_SYNTAX_PARAMETER,
		 * before the three connection children (source order). */
		param = pigen_syntax_get(&p.tree, inst->first_child);
		REQUIRE(param && param->kind == PIGEN_SYNTAX_PARAMETER);
		REQUIRE(param->as.parameter.is_local == 0);
		REQUIRE(token_is(&p.pre.expanded, param->as.parameter.name, "W"));
		REQUIRE(expr_span_is(&p.sources, &p.tree, param->as.parameter.value, "8"));

		/* The three connection children, in source order: .din(a), b,
		 * .clk(clk). */
		{
			const pigen_syntax_node *c0, *c1, *c2;
			pigen_syntax_id c = inst->first_child;
			c = pigen_syntax_get(&p.tree, c)->next_sibling;
			c0 = pigen_syntax_get(&p.tree, c);
			c = c0->next_sibling;
			c1 = pigen_syntax_get(&p.tree, c);
			c = c1->next_sibling;
			c2 = pigen_syntax_get(&p.tree, c);

			REQUIRE(c0 && c0->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
			REQUIRE(token_is(&p.pre.expanded, c0->as.instance_connection.port, "din"));
			REQUIRE(expr_span_is(&p.sources, &p.tree,
				c0->as.instance_connection.expression, "a"));
			REQUIRE(span_is_range(&p.sources, c0->location.source_span, p.source,
				text, occurrence_start(text, ".din(a)", 1),
				occurrence_start(text, ".din(a)", 1) + strlen(".din(a)")));

			REQUIRE(c1 && c1->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
			REQUIRE(IS_INVALID_ID(c1->as.instance_connection.port));
			REQUIRE(expr_span_is(&p.sources, &p.tree,
				c1->as.instance_connection.expression, "b"));
			REQUIRE(span_is_range(&p.sources, c1->location.source_span, p.source,
				text, occurrence_start(text, "b", 2),
				occurrence_start(text, "b", 2) + strlen("b")));

			REQUIRE(c2 && c2->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
			REQUIRE(token_is(&p.pre.expanded, c2->as.instance_connection.port, "clk"));
			REQUIRE(expr_span_is(&p.sources, &p.tree,
				c2->as.instance_connection.expression, "clk"));
			REQUIRE(c2->next_sibling.index == PIGEN_INVALID_ID);
		}
		/* The whole item's original span, from the type name `ram` through the
		 * terminating `;`. */
		REQUIRE(span_is_range(&p.sources, inst->location.source_span, p.source,
			text, occurrence_start(text, "ram", 1),
			occurrence_start(text, "clk));", 1) + strlen("clk));")));
		in_free(&p);
	}

	/* ------------------------------------------------------------------ */
	/* (b) empty-params-named-only: an empty parameter list and only named */
	/*     connections parse to a zero-parameter instance.                */
	/* ------------------------------------------------------------------ */
	SECTION("empty-params-named-only") {
		const char *text =
			"module top(input logic clk);\n"
			"\tram #( ) r2 (.clk(clk), .en(1));\n"
			"endmodule";
		parsed p;
		const pigen_syntax_node *inst;
		const pigen_syntax_node *c0, *c1;
		pigen_syntax_id c;

		in_parse(&p, "b.pigen", text);
		REQUIRE(p.error.message == NULL);
		inst = p.instance;
		REQUIRE(inst && inst->kind == PIGEN_SYNTAX_INSTANCE);
		REQUIRE(token_is(&p.pre.expanded, inst->as.instance.type_name, "ram"));
		REQUIRE(token_is(&p.pre.expanded, inst->as.instance.name, "r2"));
		REQUIRE(inst->as.instance.parameter_count == 0);
		REQUIRE(inst->as.instance.connection_count == 2);

		c = inst->first_child;
		c0 = pigen_syntax_get(&p.tree, c);
		c = c0->next_sibling;
		c1 = pigen_syntax_get(&p.tree, c);

		REQUIRE(c0 && c0->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
		REQUIRE(token_is(&p.pre.expanded, c0->as.instance_connection.port, "clk"));
		REQUIRE(expr_span_is(&p.sources, &p.tree,
			c0->as.instance_connection.expression, "clk"));
		REQUIRE(c1 && c1->kind == PIGEN_SYNTAX_INSTANCE_CONNECTION);
		REQUIRE(token_is(&p.pre.expanded, c1->as.instance_connection.port, "en"));
		REQUIRE(expr_span_is(&p.sources, &p.tree,
			c1->as.instance_connection.expression, "1"));
		REQUIRE(c1->next_sibling.index == PIGEN_INVALID_ID);
		/* The whole-item span runs through the terminating `;`. */
		REQUIRE(span_is_range(&p.sources, inst->location.source_span, p.source,
			text, occurrence_start(text, "ram", 1),
			occurrence_start(text, "1));", 1) + strlen("1));")));
		in_free(&p);
	}

	/* ------------------------------------------------------------------ */
	/* (c) fail-loud-reds: missing ';', unterminated connection list, and */
	/*     a malformed connection each report at the offending span.      */
	/* ------------------------------------------------------------------ */
	SECTION("fail-loud-reds") {
		/* Missing ';': the diagnostic is at the token where ';' is expected. */
		{
			const char *text =
				"module top(input logic clk);\n"
				"\tram r1 (.clk(clk))\n"
				"endmodule";
			parsed p;

			in_parse(&p, "c1.pigen", text);
			REQUIRE(p.error.message);
			REQUIRE(span_is(&p.sources, p.error.span, "endmodule"));
			in_free(&p);
		}
		/* Unterminated connection list: the diagnostic is at the connection
		 * list's opening '('. */
		{
			const char *text =
				"module top(input logic clk);\n"
				"\tram r1 (.clk(clk)\n"
				"endmodule";
			parsed p;

			in_parse(&p, "c2.pigen", text);
			REQUIRE(p.error.message);
			REQUIRE(span_is(&p.sources, p.error.span, "("));
			in_free(&p);
		}
		/* A connection that is neither .PORT(expr) nor an expression: a '.'
		 * not followed by an identifier and '(' is a malformed connection,
		 * reported at the '.'. */
		{
			const char *text =
				"module top(input logic clk);\n"
				"\tram r1 (. (clk));\n"
				"endmodule";
			parsed p;

			in_parse(&p, "c3.pigen", text);
			REQUIRE(p.error.message);
			REQUIRE(span_is(&p.sources, p.error.span, "."));
			in_free(&p);
		}
	}

	/* ------------------------------------------------------------------ */
	/* (d) data-first-not-instance: a data-first declaration beginning    */
	/*     identifier, identifier, '(' never parses as a silent instance */
	/*     node.                                                         */
	/* ------------------------------------------------------------------ */
	SECTION("data-first-not-instance") {
		/* A Pigen data-first declaration whose type is a Pigen count type:
		 * `int[16] buf (x)` is decisive (PIGEN ownership) so the instance
		 * dispatch must not fire; it takes the declaration route and fails
		 * loudly at the '(' (a declarator cannot be followed by '('). No
		 * instance node is published. */
		{
			const char *text =
				"module top(input logic clk);\n"
				"\tint[16] buf (x);\n"
				"endmodule";
			parsed p;

			in_parse(&p, "d1.pigen", text);
			REQUIRE(p.instance == NULL);
			REQUIRE(p.error.message);
			REQUIRE(span_is(&p.sources, p.error.span, "("));
			in_free(&p);
		}
		/* A structured-typedef data-first declaration: `typedef` in scope
		 * makes the type STRUCTURED (decisive), so the instance dispatch must
		 * not fire; the declaration route fails loudly at the '(' and no
		 * instance node is published. */
		{
			const char *text =
				"module top(input logic clk);\n"
				"\ttypedef logic [7:0] word_t;\n"
				"\tword_t buf (x);\n"
				"endmodule";
			parsed p;

			in_parse(&p, "d2.pigen", text);
			REQUIRE(p.instance == NULL);
			REQUIRE(p.error.message);
			REQUIRE(span_is(&p.sources, p.error.span, "("));
			in_free(&p);
		}
	}

	return check_finish();
}
