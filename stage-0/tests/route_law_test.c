/* Retained-core projection/concat/co-slice law test contract.
 *
 * Test-contract stage of the retained-core projection/concat/co-slice law
 * work item. It pins the promised behavior of the route (include/pigen/route.h)
 * for the three law families the spec and the gates leaf require of the
 * retained core:
 *
 *   - projection: a payload projection consumes the complete base token
 *     (an unpeeked select reads the whole base signal, not a sub-slice), and
 *     repeated reads of one base deduplicate to a single consuming source;
 *   - concatenation: a `{a, b} <= c` transfer is a packed bit stream,
 *     leftmost most significant, and the aggregate widths of the two sides
 *     must agree;
 *   - co-slice: a `{a, b} <= {c, d}` transfer is a packed bit stream whose
 *     member order is preserved on both sides.
 *
 * Every witness is built directly through the owner APIs on the landed route
 * and the shared signal arena/data-type owner algebra - never through the
 * quarantined textual prototype. Sections that depend on the yet-unbuilt law
 * behavior compile and run today, fail at the promised behavior, and pass
 * once the implementation lands. The manifest (tests/contracts.json, one
 * entry per section, wired into the route-test target) is the acceptance
 * gate, green on master.
 */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pigen/predicate.h"
#include "pigen/rtl_lower.h"
#include "pigen/route.h"
#include "pigen/semantic.h"
#include "pigen/source.h"
#include "pigen/transfer_type.h"

#define IS_INVALID_ID(id) ((id).index == PIGEN_INVALID_ID)
#define IS_INVALID_SPAN(span) ((span).source.index == PIGEN_INVALID_ID)
#define SPAN_EQ(a,b) ((a).source.index==(b).source.index \
	&& (a).start==(b).start && (a).end==(b).end)

/* lw_unit: the complete resolved unit - one module, a clock (LOGIC input)
 * and one clocked process on the clock domain. The witness signals,
 * transfers and their spans are added by the caller through the owner APIs.
 * Fills *out. The caller owns model/sources/rtl/lowering and must free them.
 * Every REQUIRE here runs inside the calling section's setjmp scope. */
struct lw_unit {
	pigen_module_id module;
	pigen_symbol_id clk_symbol;
	pigen_signal_id clk;
	pigen_clock_domain_id domain;
	pigen_process_id process;
	pigen_data_type_id t1;
	pigen_source_id source;
	pigen_source_span whole;
};

static struct lw_unit lw_unit(pigen_semantic_model *m,
	pigen_source_manager *srcs, pigen_rtl_model *rtl,
	pigen_rtl_lowering *lowering, const char *text)
{
	struct lw_unit u;
	pigen_source_span name_clk, process_span;
	pigen_scope_id module_scope;
	pigen_symbol_id module_symbol, clk_symbol;
	pigen_expr_id clk_expr;

	memset(&u, 0, sizeof u);
	pigen_semantic_init(m, srcs);
	pigen_rtl_lowering_init(lowering, m, rtl);
	u.t1 = pigen_data_type_sized_logic(m, 1, PIGEN_SIGN_UNSIGNED);
	REQUIRE(!IS_INVALID_ID(u.t1));
	u.source = pigen_source_add(srcs, "route_law.pigen", text, strlen(text));
	REQUIRE(u.source.index != PIGEN_INVALID_ID);
	u.whole = (pigen_source_span){u.source, 0, strlen(text)};
	name_clk = (pigen_source_span){u.source, 19, 22}; /* "clk" */
	process_span = (pigen_source_span){u.source, 13, strlen(text)};
	m->compilation_scope = pigen_scope_add(m,
		(pigen_scope_id){PIGEN_INVALID_ID},
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
	REQUIRE(!IS_INVALID_ID(m->compilation_scope));
	REQUIRE(pigen_symbol_declare(m, m->compilation_scope, PIGEN_SYMBOL_MODULE,
		(pigen_data_type_id){PIGEN_INVALID_ID}, u.whole, u.whole,
		&module_symbol, NULL) == PIGEN_DECLARE_OK);
	module_scope = pigen_scope_add(m, m->compilation_scope, u.whole);
	REQUIRE(!IS_INVALID_ID(module_scope));
	u.module = pigen_module_add(m, (pigen_syntax_id){1}, module_symbol,
		module_scope, u.whole);
	REQUIRE(!IS_INVALID_ID(u.module));
	REQUIRE(pigen_symbol_declare(m, module_scope, PIGEN_SYMBOL_SIGNAL, u.t1,
		name_clk, name_clk, &clk_symbol, NULL) == PIGEN_DECLARE_OK);
	u.clk_symbol = clk_symbol;
	u.clk = pigen_signal_add(m, (pigen_syntax_id){2}, u.module, clk_symbol,
		u.t1, pigen_semantic_scalar_shape(m), (pigen_expr_id){PIGEN_INVALID_ID},
		PIGEN_TRANSFER_TYPE_LOGIC, PIGEN_SEMANTIC_INPUT, name_clk);
	REQUIRE(!IS_INVALID_ID(u.clk));
	u.domain = pigen_clock_domain_intern(m, clk_symbol,
		PIGEN_SEMANTIC_POSEDGE);
	REQUIRE(!IS_INVALID_ID(u.domain));
	clk_expr = pigen_expr_add_symbol(m, clk_symbol, u.t1, name_clk);
	REQUIRE(!IS_INVALID_ID(clk_expr));
	u.process = pigen_process_add(m, (pigen_syntax_id){3}, u.module,
		u.domain, clk_expr, process_span);
	REQUIRE(!IS_INVALID_ID(u.process));
	/* Seed the one-atom true guard directly in the predicate arena (the
	 * t8-module pattern): a valid guard the transfers can consume. */
	m->predicates = malloc(sizeof(*m->predicates));
	REQUIRE(m->predicates);
	m->predicates[0] = (pigen_predicate){0};
	m->predicate_count = 1;
	m->predicate_capacity = 1;
	return u;
}

 /* lw_scope: the unit's module scope, re-derived from the module symbol. */
 static pigen_scope_id lw_scope(pigen_semantic_model *m, pigen_module_id module)
{
	const pigen_semantic_module *mod = pigen_module_get(m, module);
	REQUIRE(mod);
	return mod->scope;
}

/* lw_run: build and check the route over the unit's module and report the
 * results. A valid route builds and checks with 0, check_valid 1, one node
 * per direct atomic transfer, and an invalid (sentinel) check_span. */
static void lw_run(pigen_rtl_lowering *lowering,
	pigen_route *route, pigen_module_id module)
{
	int rc;

	pigen_route_init(route, module);
	rc = pigen_route_build(route, lowering, module);
	REQUIRE(rc == 0);
	rc = pigen_route_check(route);
	REQUIRE(rc == 0);
	REQUIRE(route->check_valid == 1);
	REQUIRE(IS_INVALID_SPAN(route->check_span));
}

int main(int argc, char **argv)
{
	pigen_source_manager sources;
	pigen_semantic_model model;
	pigen_rtl_model rtl;
	pigen_rtl_lowering lowering;
	struct lw_unit unit;
	pigen_scope_id scope;
	pigen_route route;

	check_init(argc, argv);
	sources = (pigen_source_manager){0};
	rtl = (pigen_rtl_model){0};

	/* ------------------------------------------------------------------ */
	/* (1) Projection, complete base-token consumption: `out <=          */
	/*     packet[7:0]` consumes the COMPLETE base token `packet`, not a  */
	/*     sub-slice. The source is a payload select of `packet`; the     */
	/*     base is `packet` itself. The route accepts the transfer and    */
	/*     its ready graph is one node carrying the destination signal's  */
	/*     realization-ready dependency. A route that treats the select as */
	/*     a smaller base (or rejects the projection) fails this.         */
	/* ------------------------------------------------------------------ */
	SECTION("law-projection-complete") {
		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		{
			const char *text =
				"module top : input clk : logic ; input packet : buf ; output out : buf ;\n";
			pigen_data_type_id tp, sel_type;
			pigen_signal_id packet, out;
			pigen_symbol_id packet_sym, out_sym;
			pigen_expr_id packet_e, sel_e, out_e;
			pigen_lvalue_id dest;
			pigen_transfer_id transfer;
			pigen_expr_id c7, c0;
			pigen_source_span trspan;

			unit = lw_unit(&model, &sources, &rtl, &lowering, text);
			scope = lw_scope(&model, unit.module);
			/* packet: a 16-bit BUF input - the complete base token. */
			tp = pigen_data_type_sized_logic(&model, 16, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(tp));
			{
				pigen_source_span sp_p = {unit.source, 14, 15};
				pigen_symbol_id ps;
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, tp, sp_p, sp_p, &ps, NULL)
					== PIGEN_DECLARE_OK);
				packet = pigen_signal_add(&model, (pigen_syntax_id){10},
					unit.module, ps, tp, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INPUT, sp_p);
				REQUIRE(!IS_INVALID_ID(packet));
				packet_sym = ps;
			}
			/* The source is a payload select of `packet`: bits [7:0] of the
			 * 16-bit base. Its data type is the projected 8 bits; its base is
			 * the complete `packet` token. */
			packet_e = pigen_expr_add_symbol(&model, packet_sym, tp,
				(pigen_source_span){unit.source, 14, 15});
			REQUIRE(!IS_INVALID_ID(packet_e));
			c7 = pigen_expr_add_integer(&model, 7,
				pigen_data_type_unsized_integer(&model),
				(pigen_source_span){unit.source, 18, 19});
			c0 = pigen_expr_add_integer(&model, 0,
				pigen_data_type_unsized_integer(&model),
				(pigen_source_span){unit.source, 20, 21});
			sel_e = pigen_expr_add_select(&model, packet_e, c7, c0,
				PIGEN_SEMANTIC_SELECT_RANGE,
				(pigen_source_span){unit.source, 14, 22});
			REQUIRE(!IS_INVALID_ID(sel_e));
			sel_type = pigen_expr_get(&model, sel_e)->data_type;
			REQUIRE(!IS_INVALID_ID(sel_type));
			/* The destination `out` carries exactly the projected type, so the
			 * transfer is well-typed and the projection is what is exercised. */
			{
				pigen_source_span sp_o = {unit.source, 16, 17};
				pigen_symbol_id os;
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, sel_type, sp_o, sp_o, &os, NULL)
					== PIGEN_DECLARE_OK);
				out = pigen_signal_add(&model, (pigen_syntax_id){11},
					unit.module, os, sel_type,
					pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INTERNAL, sp_o);
				REQUIRE(!IS_INVALID_ID(out));
				out_sym = os;
			}
			/* The destination is a projection lvalue onto `out`. */
			out_e = pigen_expr_add_symbol(&model, out_sym, sel_type,
				(pigen_source_span){unit.source, 16, 17});
			REQUIRE(!IS_INVALID_ID(out_e));
			dest = pigen_lvalue_resolve(&model, out_e);
			REQUIRE(!IS_INVALID_ID(dest));
			trspan = (pigen_source_span){unit.source, 14, 22};
			transfer = pigen_transfer_add(&model, (pigen_syntax_id){12},
				unit.module, unit.process, dest, sel_e,
				(pigen_predicate_id){0}, unit.domain, NULL, 0, trspan);
			REQUIRE(!IS_INVALID_ID(transfer));
			lw_run(&lowering, &route, unit.module);
			/* One ready-graph node, carrying the destination signal's
			 * realization-ready dependency (a BUF realizes an elastic slot:
			 * DOWNSTREAM). */
			REQUIRE(route.node_count == 1);
			REQUIRE(route.nodes[0].ready_dependency ==
				PIGEN_TRANSFER_READY_DOWNSTREAM);
		}
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (2) Projection, repeated-read deduplication: two transfers that     */
	/*     both read the SAME base token `packet` deduplicate to a SINGLE  */
	/*     consuming source. The transfer's deduplicated signal-use set     */
	/*     names `packet` once (one consuming source), not once per read.   */
	/*     A route that counts a consumer per read (two consuming sources)  */
	/*     fails this.                                                     */
	/* ------------------------------------------------------------------ */
	SECTION("law-projection-dedup") {
		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		{
			const char *text =
				"module top : input clk : logic ; input packet : buf ; output a : buf ; output b : buf ;\n";
			pigen_data_type_id tp;
			pigen_signal_id packet, pa, pb;
			pigen_symbol_id ps, as, bs;
			pigen_expr_id pe, ae, be;
			pigen_lvalue_id da, db;
			pigen_transfer_id tr_a, tr_b;
			const pigen_transfer_signal_use *uses;
			size_t use_count, i;

			unit = lw_unit(&model, &sources, &rtl, &lowering, text);
			scope = lw_scope(&model, unit.module);
			tp = pigen_data_type_sized_logic(&model, 16, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(tp));
			{
				pigen_source_span sp_p = {unit.source, 14, 15};
				pigen_source_span sp_a = {unit.source, 16, 17};
				pigen_source_span sp_b = {unit.source, 18, 19};
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, tp, sp_p, sp_p, &ps, NULL)
					== PIGEN_DECLARE_OK);
				packet = pigen_signal_add(&model, (pigen_syntax_id){10},
					unit.module, ps, tp, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INPUT, sp_p);
				REQUIRE(!IS_INVALID_ID(packet));
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, tp, sp_a, sp_a, &as, NULL)
					== PIGEN_DECLARE_OK);
				pa = pigen_signal_add(&model, (pigen_syntax_id){11},
					unit.module, as, tp, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INTERNAL, sp_a);
				REQUIRE(!IS_INVALID_ID(pa));
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, tp, sp_b, sp_b, &bs, NULL)
					== PIGEN_DECLARE_OK);
				pb = pigen_signal_add(&model, (pigen_syntax_id){12},
					unit.module, bs, tp, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INTERNAL, sp_b);
				REQUIRE(!IS_INVALID_ID(pb));
			}
			/* Both transfers read the SAME base token `packet` (a complete
			 * base-token read); they write distinct destinations a and b.
			 * Each transfer's deduplicated signal-use set names `packet`
			 * once as its consuming source - repeated reads of one base
			 * consume once. */
			pe = pigen_expr_add_symbol(&model, ps, tp,
				(pigen_source_span){unit.source, 14, 15});
			REQUIRE(!IS_INVALID_ID(pe));
			ae = pigen_expr_add_symbol(&model, as, tp,
				(pigen_source_span){unit.source, 16, 17});
			be = pigen_expr_add_symbol(&model, bs, tp,
				(pigen_source_span){unit.source, 18, 19});
			REQUIRE(!IS_INVALID_ID(ae) && !IS_INVALID_ID(be));
			da = pigen_lvalue_resolve(&model, ae);
			db = pigen_lvalue_resolve(&model, be);
			REQUIRE(!IS_INVALID_ID(da) && !IS_INVALID_ID(db));
			/* Each transfer carries one consuming source: the shared base
			 * `packet` (repeated reads of one base deduplicate to a single
			 * consuming source). */
			{
				pigen_transfer_signal_use uses_a[1];
				pigen_transfer_signal_use uses_b[1];
				uses_a[0] = (pigen_transfer_signal_use){packet,
					PIGEN_TRANSFER_CONSUMER};
				uses_b[0] = (pigen_transfer_signal_use){packet,
					PIGEN_TRANSFER_CONSUMER};
				tr_a = pigen_transfer_add(&model, (pigen_syntax_id){13},
					unit.module, unit.process, da, pe,
					(pigen_predicate_id){0}, unit.domain, uses_a, 1,
					(pigen_source_span){unit.source, 14, 19});
				tr_b = pigen_transfer_add(&model, (pigen_syntax_id){14},
					unit.module, unit.process, db, pe,
					(pigen_predicate_id){0}, unit.domain, uses_b, 1,
					(pigen_source_span){unit.source, 14, 19});
			}
			REQUIRE(!IS_INVALID_ID(tr_a) && !IS_INVALID_ID(tr_b));
			lw_run(&lowering, &route, unit.module);
			/* The route mirrors both transfers: two ready-graph nodes in
			 * arena order. */
			REQUIRE(route.node_count == 2);
			REQUIRE(route.nodes[0].transfer.index == tr_a.index);
			REQUIRE(route.nodes[1].transfer.index == tr_b.index);
			/* Each transfer's deduplicated consumer set names the shared
			 * base `packet` exactly once (one consuming source, not one per
			 * read). A route that double-counts the base as two consuming
			 * sources fails this. */
			for (i = 0; i < 2; i++)
			{
				pigen_transfer_id tr = (i == 0) ? tr_a : tr_b;
				uses = pigen_transfer_signal_uses(&model, tr);
				REQUIRE(uses);
				use_count = pigen_transfer_get(&model, tr)->signal_use_count;
				{
					size_t base_consumers = 0, j;
					for (j = 0; j < use_count; j++)
						if (uses[j].signal.index == packet.index &&
							uses[j].roles & PIGEN_TRANSFER_CONSUMER)
							base_consumers++;
					REQUIRE(base_consumers == 1);
				}
			}
		}
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (3) Concatenation, packed bit-stream order: `{a, b} <= {x, y}` is a */
	/*     packed bit stream, leftmost most significant; the aggregate      */
	/*     widths of the two sides must agree (two 4-bit halves over 8 bits). */
	/*     The route accepts the transfer and its ready graph is one node. A */
	/*     route that rejects the concatenation (or does not check the       */
	/*     aggregate width) fails this.                                     */
	/* ------------------------------------------------------------------ */
	SECTION("law-concat-order") {
		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		{
			const char *text =
				"module top : input clk : logic ; input x : buf ; input y : buf ; output a : buf ; output b : buf ;\n";
			pigen_data_type_id t4, cat;
			pigen_signal_id px, py, pa, pb;
			pigen_symbol_id xs, ys, as, bs;
			pigen_expr_id xe, ye, ae, be, cat_e;
			pigen_lvalue_id destc;
			const pigen_semantic_lvalue *lval;
			const pigen_expr_id *expr_children;
			const pigen_lvalue_id *lvalue_children;
			pigen_expr_id cat_children[2];
			pigen_data_type_id cat_types[2];
			pigen_transfer_id transfer;

			unit = lw_unit(&model, &sources, &rtl, &lowering, text);
			scope = lw_scope(&model, unit.module);
			t4 = pigen_data_type_sized_logic(&model, 4, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(t4));
			{
				pigen_source_span sp_p = {unit.source, 14, 15};
				pigen_source_span sp_q = {unit.source, 16, 17};
				pigen_source_span sp_a = {unit.source, 18, 19};
				pigen_source_span sp_b = {unit.source, 20, 21};
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, t4, sp_p, sp_p, &xs, NULL)
					== PIGEN_DECLARE_OK);
				px = pigen_signal_add(&model, (pigen_syntax_id){10},
					unit.module, xs, t4, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INPUT, sp_p);
				REQUIRE(!IS_INVALID_ID(px));
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, t4, sp_q, sp_q, &ys, NULL)
					== PIGEN_DECLARE_OK);
				py = pigen_signal_add(&model, (pigen_syntax_id){11},
					unit.module, ys, t4, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INPUT, sp_q);
				REQUIRE(!IS_INVALID_ID(py));
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, t4, sp_a, sp_a, &as, NULL)
					== PIGEN_DECLARE_OK);
				pa = pigen_signal_add(&model, (pigen_syntax_id){12},
					unit.module, as, t4, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INTERNAL, sp_a);
				REQUIRE(!IS_INVALID_ID(pa));
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, t4, sp_b, sp_b, &bs, NULL)
					== PIGEN_DECLARE_OK);
				/* b is a LOGIC (procedural variable) destination, whose
				 * realization-ready dependency is CONSTANT, while a is a BUF
				 * (elastic slot, DOWNSTREAM): the two halves of the
				 * destination carry different ready dependencies, so the
				 * ready-graph node's ready dependency pins WHICH half the
				 * route resolves as the destination signal. */
				pb = pigen_signal_add(&model, (pigen_syntax_id){13},
					unit.module, bs, t4, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
					PIGEN_SEMANTIC_INTERNAL, sp_b);
				REQUIRE(!IS_INVALID_ID(pb));
			}
			/* The destination is a concatenation lvalue `{a, b}`: a is the
			 * most-significant half, b the least-significant half. The
			 * aggregate data type is the 8-bit concatenation of the two
			 * 4-bit halves. */
			ae = pigen_expr_add_symbol(&model, as, t4,
				(pigen_source_span){unit.source, 18, 19});
			be = pigen_expr_add_symbol(&model, bs, t4,
				(pigen_source_span){unit.source, 20, 21});
			REQUIRE(!IS_INVALID_ID(ae) && !IS_INVALID_ID(be));
			cat_children[0] = ae;
			cat_children[1] = be;
			cat_e = pigen_expr_add_concatenation(&model, cat_children, 2,
				(pigen_source_span){unit.source, 18, 21});
			REQUIRE(!IS_INVALID_ID(cat_e));
			/* The aggregate data type is the concatenation of the two halves. */
			cat_types[0] = t4;
			cat_types[1] = t4;
			cat = pigen_data_type_concatenation(&model, cat_types, 2);
			REQUIRE(!IS_INVALID_ID(cat));
			{
				uint64_t cat_width;
				REQUIRE(pigen_const_expr_evaluate_u64(&model,
					pigen_data_type_packed_width(&model, cat), &cat_width));
				REQUIRE(cat_width == 8);
			}
			destc = pigen_lvalue_resolve(&model, cat_e);
			REQUIRE(!IS_INVALID_ID(destc));
			/* The source is the value bit stream `{x, y}`: x
			 * most-significant, y least-significant. It is a packed bit
			 * stream whose aggregate width (8) agrees with the destination's. */
			xe = pigen_expr_add_symbol(&model, xs, t4,
				(pigen_source_span){unit.source, 14, 15});
			ye = pigen_expr_add_symbol(&model, ys, t4,
				(pigen_source_span){unit.source, 16, 17});
			REQUIRE(!IS_INVALID_ID(xe) && !IS_INVALID_ID(ye));
			{
				pigen_expr_id src_children[2];
				src_children[0] = xe;
				src_children[1] = ye;
				transfer = pigen_transfer_add(&model, (pigen_syntax_id){14},
					unit.module, unit.process, destc,
					pigen_expr_add_concatenation(&model, src_children, 2,
						(pigen_source_span){unit.source, 14, 17}),
					(pigen_predicate_id){0}, unit.domain, NULL, 0,
					(pigen_source_span){unit.source, 14, 21});
			}
			REQUIRE(!IS_INVALID_ID(transfer));
			lw_run(&lowering, &route, unit.module);
			/* One ready-graph node for the concatenation transfer. */
			REQUIRE(route.node_count == 1);
			/* Order-discriminating positive witness. The destination
			 * concatenation lvalue's children are [a, b]: a (BUF) is the
			 * most-significant (leftmost) child, b (LOGIC) the
			 * least-significant. The source concatenation's children are in
			 * the same left-to-right (MSB-to-LSB) order: [x, y]. And the
			 * ready-graph node carries the LEFTMOST destination signal's
			 * realization-ready dependency - a's (BUF: DOWNSTREAM), not b's
			 * (LOGIC: CONSTANT). A route that reverses the member order (or
			 * resolves the least-significant destination child) reports
			 * CONSTANT and fails here. */
			lval = pigen_lvalue_get(&model, destc);
			REQUIRE(lval && lval->kind == PIGEN_LVALUE_CONCATENATION);
			REQUIRE(lval->as.sequence.child_count == 2);
			lvalue_children = pigen_lvalue_children(&model,
				lval->as.sequence.first_child, lval->as.sequence.child_count);
			REQUIRE(lvalue_children);
			{
				const pigen_semantic_lvalue *child_a, *child_b;

				child_a = pigen_lvalue_get(&model, lvalue_children[0]);
				child_b = pigen_lvalue_get(&model, lvalue_children[1]);
				REQUIRE(child_a && child_a->kind == PIGEN_LVALUE_PROJECTION);
				REQUIRE(child_b && child_b->kind == PIGEN_LVALUE_PROJECTION);
				REQUIRE(child_a->as.projection.signal.index == pa.index);
				REQUIRE(child_b->as.projection.signal.index == pb.index);
			}
			expr_children = pigen_expr_children(&model,
				pigen_expr_get(&model, cat_e)->as.sequence.first_child,
				pigen_expr_get(&model, cat_e)->as.sequence.child_count);
			REQUIRE(expr_children);
			REQUIRE(pigen_expr_get(&model, expr_children[0])->as.symbol.index
				== as.index);
			REQUIRE(pigen_expr_get(&model, expr_children[1])->as.symbol.index
				== bs.index);
			REQUIRE(route.nodes[0].transfer.index == transfer.index);
			REQUIRE(route.nodes[0].ready_dependency ==
				PIGEN_TRANSFER_READY_DOWNSTREAM);
		}
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (4) Co-slice, packed bit-stream order preserved: `{a, b} <= {c, d}`  */
	/*     is a packed bit stream on BOTH sides whose member order is        */
	/*     preserved (a<-c most-significant, b<-d least-significant). The   */
	/*     route accepts the transfer and its ready graph is one node. A     */
	/*     route that rejects the co-slice (or reverses the member order)    */
	/*     fails this.                                                      */
	/* ------------------------------------------------------------------ */
	SECTION("law-coslice-order") {
		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		{
			const char *text =
				"module top : input clk : logic ; input c : buf ; input d : buf ; output a : buf ; output b : buf ;\n";
			pigen_data_type_id t4;
			pigen_signal_id pc, pd, pa, pb;
			pigen_symbol_id cs, ds, as, bs;
			pigen_expr_id ce, de, ae, be;
			pigen_expr_id dest_children[2], src_children[2];
			pigen_expr_id dest_e, src_e;
			pigen_lvalue_id destc;
			const pigen_semantic_lvalue *lval;
			const pigen_expr_id *expr_children;
			const pigen_lvalue_id *lvalue_children;
			pigen_transfer_id transfer;

			unit = lw_unit(&model, &sources, &rtl, &lowering, text);
			scope = lw_scope(&model, unit.module);
			t4 = pigen_data_type_sized_logic(&model, 4, PIGEN_SIGN_UNSIGNED);
			REQUIRE(!IS_INVALID_ID(t4));
			{
				pigen_source_span sp_c = {unit.source, 14, 15};
				pigen_source_span sp_d = {unit.source, 16, 17};
				pigen_source_span sp_a = {unit.source, 18, 19};
				pigen_source_span sp_b = {unit.source, 20, 21};
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, t4, sp_c, sp_c, &cs, NULL)
					== PIGEN_DECLARE_OK);
				pc = pigen_signal_add(&model, (pigen_syntax_id){10},
					unit.module, cs, t4, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INPUT, sp_c);
				REQUIRE(!IS_INVALID_ID(pc));
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, t4, sp_d, sp_d, &ds, NULL)
					== PIGEN_DECLARE_OK);
				pd = pigen_signal_add(&model, (pigen_syntax_id){11},
					unit.module, ds, t4, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INPUT, sp_d);
				REQUIRE(!IS_INVALID_ID(pd));
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, t4, sp_a, sp_a, &as, NULL)
					== PIGEN_DECLARE_OK);
				pa = pigen_signal_add(&model, (pigen_syntax_id){12},
					unit.module, as, t4, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
					PIGEN_SEMANTIC_INTERNAL, sp_a);
				REQUIRE(!IS_INVALID_ID(pa));
				REQUIRE(pigen_symbol_declare(&model, scope,
					PIGEN_SYMBOL_SIGNAL, t4, sp_b, sp_b, &bs, NULL)
					== PIGEN_DECLARE_OK);
				/* b is a LOGIC (procedural variable) destination, whose
				 * realization-ready dependency is CONSTANT, while a is a BUF
				 * (elastic slot, DOWNSTREAM): the two halves of the
				 * destination carry different ready dependencies, so the
				 * ready-graph node's ready dependency pins WHICH half the
				 * route resolves as the destination signal. */
				pb = pigen_signal_add(&model, (pigen_syntax_id){13},
					unit.module, bs, t4, pigen_semantic_scalar_shape(&model),
					(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_LOGIC,
					PIGEN_SEMANTIC_INTERNAL, sp_b);
				REQUIRE(!IS_INVALID_ID(pb));
			}
			/* The destination is the co-slice lvalue `{a, b}`: a
			 * most-significant, b least-significant. */
			ae = pigen_expr_add_symbol(&model, as, t4,
				(pigen_source_span){unit.source, 18, 19});
			be = pigen_expr_add_symbol(&model, bs, t4,
				(pigen_source_span){unit.source, 20, 21});
			REQUIRE(!IS_INVALID_ID(ae) && !IS_INVALID_ID(be));
			dest_children[0] = ae;
			dest_children[1] = be;
			dest_e = pigen_expr_add_concatenation(&model, dest_children, 2,
				(pigen_source_span){unit.source, 18, 21});
			REQUIRE(!IS_INVALID_ID(dest_e));
			/* The source is the co-slice expression `{c, d}`: c
			 * most-significant, d least-significant. */
			ce = pigen_expr_add_symbol(&model, cs, t4,
				(pigen_source_span){unit.source, 14, 15});
			de = pigen_expr_add_symbol(&model, ds, t4,
				(pigen_source_span){unit.source, 16, 17});
			REQUIRE(!IS_INVALID_ID(ce) && !IS_INVALID_ID(de));
			src_children[0] = ce;
			src_children[1] = de;
			src_e = pigen_expr_add_concatenation(&model, src_children, 2,
				(pigen_source_span){unit.source, 14, 17});
			REQUIRE(!IS_INVALID_ID(src_e));
			destc = pigen_lvalue_resolve(&model, dest_e);
			REQUIRE(!IS_INVALID_ID(destc));
			/* The two sides are packed bit streams of the same aggregate
			 * width (8), with matching member order. */
			transfer = pigen_transfer_add(&model, (pigen_syntax_id){14},
				unit.module, unit.process, destc, src_e,
				(pigen_predicate_id){0}, unit.domain, NULL, 0,
				(pigen_source_span){unit.source, 14, 21});
			REQUIRE(!IS_INVALID_ID(transfer));
			lw_run(&lowering, &route, unit.module);
			/* One ready-graph node for the co-slice transfer. */
			REQUIRE(route.node_count == 1);
			/* Order-discriminating positive witness. The destination
			 * co-slice lvalue's children are [a, b]: a (BUF) the
			 * most-significant (leftmost) child, b (LOGIC) the
			 * least-significant. The source co-slice expression's children
			 * are in the same left-to-right (MSB-to-LSB) order: [c, d]. And
			 * the ready-graph node carries the LEFTMOST destination
			 * signal's realization-ready dependency - a's (BUF:
			 * DOWNSTREAM), not b's (LOGIC: CONSTANT). A route that reverses
			 * the member order (or resolves the least-significant
			 * destination child) reports CONSTANT and fails here. */
			lval = pigen_lvalue_get(&model, destc);
			REQUIRE(lval && lval->kind == PIGEN_LVALUE_CONCATENATION);
			REQUIRE(lval->as.sequence.child_count == 2);
			lvalue_children = pigen_lvalue_children(&model,
				lval->as.sequence.first_child, lval->as.sequence.child_count);
			REQUIRE(lvalue_children);
			{
				const pigen_semantic_lvalue *child_a, *child_b;

				child_a = pigen_lvalue_get(&model, lvalue_children[0]);
				child_b = pigen_lvalue_get(&model, lvalue_children[1]);
				REQUIRE(child_a && child_a->kind == PIGEN_LVALUE_PROJECTION);
				REQUIRE(child_b && child_b->kind == PIGEN_LVALUE_PROJECTION);
				REQUIRE(child_a->as.projection.signal.index == pa.index);
				REQUIRE(child_b->as.projection.signal.index == pb.index);
			}
			expr_children = pigen_expr_children(&model,
				pigen_expr_get(&model, src_e)->as.sequence.first_child,
				pigen_expr_get(&model, src_e)->as.sequence.child_count);
			REQUIRE(expr_children);
			REQUIRE(pigen_expr_get(&model, expr_children[0])->as.symbol.index
				== cs.index);
			REQUIRE(pigen_expr_get(&model, expr_children[1])->as.symbol.index
				== ds.index);
			REQUIRE(route.nodes[0].transfer.index == transfer.index);
			REQUIRE(route.nodes[0].ready_dependency ==
				PIGEN_TRANSFER_READY_DOWNSTREAM);
		}
		pigen_route_free(&route);
	}

	return check_finish();
}
