/* Retained-core stall / throughput law test contract.
 *
 * Test-contract stage of the retained-core stall/throughput law work item,
 * the final bounded step of the retained core after the landed route, the
 * landed projection/concat/co-slice laws and the landed
 * validity/domain/ownership laws (all green on master: make route-test,
 * make route-law-test, make route-vdo-test). It pins the promised behavior of
 * the route (include/pigen/route.h, pigen_route_init / pigen_route_build /
 * pigen_route_check / pigen_route_free) for the stall/backpressure and
 * throughput law families the spec and the gates leaf require of the retained
 * core:
 *
 *   - stall / backpressure, ready propagation: the route's whole-unit ready
 *     graph carries, for each transfer whose destination realizes a
 *     DOWNSTREAM ready dependency (a buf / elastic slot), the downstream edge
 *     naming the transfer that consumes that buffered destination. The
 *     upstream transfer's readiness propagates through the buffered
 *     destination to its consumer, so a stalled downstream (a consumer that
 *     is not ready) blocks its upstream source.
 *   - stall / backpressure, buffered ready breaks: a buffered destination
 *     whose realization-ready dependency is OCCUPANCY (a skid / skid queue)
 *     breaks the ready chain: its readiness depends on its own registered
 *     occupancy, not on the downstream, so the transfer writing it carries no
 *     downstream edge. A skid deliberately breaks ready chains (absorbs
 *     delayed backpressure).
 *   - stall / backpressure, unbroken ready cycle is an error: the route
 *     validates its whole-unit ready graph is cycle / self-loop-free before
 *     it is published; a valid acyclic route is accepted. A constructible
 *     cycle is not expressible through the owner APIs at this stage (the
 *     published ready graph's downstream edges only ever name a consumer of a
 *     DOWNSTREAM destination, and a deliberate ready break never names an
 *     edge), so this law is pinned by the acyclicity of every accepted
 *     route, not by a constructible negative witness.
 *   - throughput, per-cycle transfer and ready behavior: the route's node
 *     carries the destination signal's owner-reported realization-ready
 *     dependency and the downstream edge together, so the per-cycle transfer
 *     and ready behavior of the buffered (buf) and skid realizations is
 *     pinned against the owner catalogue: a buf destination is DOWNSTREAM
 *     (ready propagates; the buffered slot's input-ready is ~packet_valid |
 *     out_ready, so a stalled downstream blocks its upstream source) and a
 *     skid destination is OCCUPANCY (registered-occupancy ready, fixed
 *     capacity two, ready chain broken).
 *
 * Every witness is built directly through the owner APIs on the retained core
 * - include/pigen/route.h plus the shared signal arena / data-type owner
 * algebra (pigen_lower_rtl_module, pigen_lower_rtl_transfers,
 * pigen_transfer_type_descriptor_get, pigen_transfer_realization_descriptor_get,
 * pigen_transfer_ready_dependency, pigen_module_id, pigen_transfer_id,
 * pigen_source_span) - never through the quarantined textual prototype.
 * Sections that depend on the yet-unbuilt ready-edge law behavior compile and
 * run today, fail at the promised behavior (the current build publishes the
 * ready graph with no downstream edge, so the promised edge is absent and the
 * promised ready break is not pinned), and pass once the implementation lands.
 * The manifest (tests/contracts.json, one entry per section, wired into the
 * route-stall-test target) is the acceptance gate. Every section must
 * discriminate: a route that names a ready edge for an OCCUPANCY destination,
 * that omits the DOWNSTREAM edge for a buf, or that over-rejects a legal
 * acyclic buf pipeline fails its section, and a route that never names a
 * downstream edge fails the ready-propagation sections.
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

/* sttp_realization_ready: the owner-reported ready dependency for a signal's
 * transfer type, via the realization descriptor (the raw material the route
 * node carries and the stall/throughput laws consume). */
static pigen_transfer_ready_dependency sttp_realization_ready(
	pigen_transfer_type transfer)
{
	const pigen_transfer_type_descriptor *d =
		pigen_transfer_type_descriptor_get(transfer);
	const pigen_transfer_realization_descriptor *r = d ?
		pigen_transfer_realization_descriptor_get(d->realization) : NULL;
	return r ? r->ready_dependency : PIGEN_TRANSFER_READY_INVALID;
}

/* sttp_realization: the owner-reported realization for a signal's transfer
 * type, so a witness can pin which backend-neutral realization a spelling
 * carries (a skid realizes a skid queue, a buf an elastic slot). */
static pigen_transfer_realization sttp_realization(pigen_transfer_type transfer)
{
	const pigen_transfer_type_descriptor *d =
		pigen_transfer_type_descriptor_get(transfer);
	return d ? d->realization : PIGEN_TRANSFER_REALIZATION_INVALID;
}

/* sttp_capacity: the owner-reported fixed capacity of a signal's transfer
 * type realization (a skid queue is fixed two, an elastic slot fixed one), or
 * zero when the realization carries no fixed capacity. */
static size_t sttp_capacity(pigen_transfer_type transfer)
{
	const pigen_transfer_type_descriptor *d =
		pigen_transfer_type_descriptor_get(transfer);
	const pigen_transfer_realization_descriptor *r = d ?
		pigen_transfer_realization_descriptor_get(d->realization) : NULL;
	return r ? r->fixed_capacity : 0;
}

/* sttp_base: the complete resolved unit's skeleton - one module, a clock
 * (LOGIC input) and one clocked process on the clock domain, plus the seeded
 * one-atom true guard. The witness signals, transfers and their spans are
 * added by the caller through the owner APIs. The caller owns model/sources/
 * rtl/lowering and must free them. Every REQUIRE here runs inside the calling
 * section's setjmp scope. The text's signal name spans are fixed by the base
 * text below (s at 39, d at 56, e at 73); clk is at 19. */
struct sttp_base {
	pigen_module_id module;
	pigen_symbol_id clk_symbol;
	pigen_signal_id clk;
	pigen_clock_domain_id domain;
	pigen_process_id process;
	pigen_data_type_id t1;
	pigen_source_id source;
	pigen_source_span whole;
	pigen_source_span process_span;
};

static struct sttp_base sttp_base(pigen_semantic_model *m,
	pigen_source_manager *srcs, pigen_rtl_model *rtl,
	pigen_rtl_lowering *lowering, const char *text)
{
	struct sttp_base u;
	pigen_source_span name_clk;
	pigen_scope_id module_scope;
	pigen_symbol_id module_symbol;
	pigen_expr_id clk_expr;

	memset(&u, 0, sizeof u);
	pigen_semantic_init(m, srcs);
	pigen_rtl_lowering_init(lowering, m, rtl);
	u.t1 = pigen_data_type_sized_logic(m, 1, PIGEN_SIGN_UNSIGNED);
	REQUIRE(!IS_INVALID_ID(u.t1));
	u.source = pigen_source_add(srcs, "sttp_law.pigen", text, strlen(text));
	REQUIRE(u.source.index != PIGEN_INVALID_ID);
	u.whole = (pigen_source_span){u.source, 0, strlen(text)};
	name_clk = (pigen_source_span){u.source, 19, 22}; /* "clk" */
	u.process_span = (pigen_source_span){u.source, 13, strlen(text)};
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
		name_clk, name_clk, &u.clk_symbol, NULL) == PIGEN_DECLARE_OK);
	u.clk = pigen_signal_add(m, (pigen_syntax_id){2}, u.module, u.clk_symbol,
		u.t1, pigen_semantic_scalar_shape(m), (pigen_expr_id){PIGEN_INVALID_ID},
		PIGEN_TRANSFER_TYPE_LOGIC, PIGEN_SEMANTIC_INPUT, name_clk);
	REQUIRE(!IS_INVALID_ID(u.clk));
	u.domain = pigen_clock_domain_intern(m, u.clk_symbol,
		PIGEN_SEMANTIC_POSEDGE);
	REQUIRE(!IS_INVALID_ID(u.domain));
	clk_expr = pigen_expr_add_symbol(m, u.clk_symbol, u.t1, name_clk);
	REQUIRE(!IS_INVALID_ID(clk_expr));
	u.process = pigen_process_add(m, (pigen_syntax_id){3}, u.module,
		u.domain, clk_expr, u.process_span);
	REQUIRE(!IS_INVALID_ID(u.process));
	/* Seed the one-atom true guard directly in the predicate arena (the
	 * t8-module / route_law / vdo pattern): a valid guard the transfers
	 * consume. */
	m->predicates = malloc(sizeof(*m->predicates));
	REQUIRE(m->predicates);
	m->predicates[0] = (pigen_predicate){0};
	m->predicate_count = 1;
	m->predicate_capacity = 1;
	return u;
}

/* sttp_scope: the unit's module scope, re-derived from the module symbol. */
static pigen_scope_id sttp_scope(pigen_semantic_model *m, pigen_module_id module)
{
	const pigen_semantic_module *mod = pigen_module_get(m, module);
	REQUIRE(mod);
	return mod->scope;
}

/* sttp_decl: declares one signal in the module scope, using the given
 * transfer type and direction, and returns the signal. The span is the
 * signal's own name span in the source text; the symbol is kept in *sym. */
static pigen_signal_id sttp_decl(pigen_semantic_model *m, pigen_scope_id scope,
	struct sttp_base *base, pigen_data_type_id type,
	pigen_source_span sp, pigen_transfer_type ttype,
	pigen_semantic_direction dir, pigen_symbol_id *sym)
{
	pigen_signal_id sig;

	REQUIRE(pigen_symbol_declare(m, scope, PIGEN_SYMBOL_SIGNAL, type, sp, sp,
		sym, NULL) == PIGEN_DECLARE_OK);
	sig = pigen_signal_add(m, (pigen_syntax_id){100 + (int)sp.start},
		base->module, *sym, type, pigen_semantic_scalar_shape(m),
		(pigen_expr_id){PIGEN_INVALID_ID}, ttype, dir, sp);
	REQUIRE(!IS_INVALID_ID(sig));
	return sig;
}

/* sttp_node: the ready-graph node for the named transfer (the route mirrors
 * the unit's transfers one-for-one in arena order, so the node is unique). */
static const pigen_route_ready_node *sttp_node(const pigen_route *route,
	pigen_transfer_id transfer)
{
	size_t i;

	for (i = 0; i < route->node_count; i++)
		if (route->nodes[i].transfer.index == transfer.index)
			return &route->nodes[i];
	return NULL;
}

/* sttp_pipeline: the two-transfer ready-chain witness over the base text.
 * s (buf INPUT) is the upstream source; d (the intermediate, transfer type
 * d_type) is written by T1 and consumed by T2; e (buf INTERNAL) is written by
 * T2. d_type is PIGEN_TRANSFER_TYPE_BUF for a propagating ready chain or
 * PIGEN_TRANSFER_TYPE_SKID for a ready break. All three buffered signals carry
 * the legal role pairing the landed VDO laws require of a buffered transfer:
 * a buffered read carries CONSUMER and a buffered write carries PRODUCER
 * (CONSUMER on an INPUT buf is legal, the law-projection-dedup anchor).
 * Returns the two transfer ids. */
static void sttp_pipeline(pigen_semantic_model *m, pigen_scope_id scope,
	struct sttp_base *base, pigen_transfer_type d_type,
	pigen_transfer_id *t1, pigen_transfer_id *t2)
{
	pigen_signal_id s, d, e;
	pigen_symbol_id s_sym, d_sym, e_sym;
	pigen_expr_id s_e, d_e;
	pigen_lvalue_id dest1, dest2;

	/* s: an upstream input buf (CONSUMER role, legal). */
	s = sttp_decl(m, scope, base, base->t1,
		(pigen_source_span){base->source, 39, 40},
		PIGEN_TRANSFER_TYPE_BUF, PIGEN_SEMANTIC_INPUT, &s_sym);
	/* d: the intermediate; d_type names its transfer type (buf or skid). It
	 * is written by T1 (PRODUCER) and read by T2 (CONSUMER). */
	d = sttp_decl(m, scope, base, base->t1,
		(pigen_source_span){base->source, 56, 57},
		d_type, PIGEN_SEMANTIC_INTERNAL, &d_sym);
	/* e: the final buf (PRODUCER role for T2's write). */
	e = sttp_decl(m, scope, base, base->t1,
		(pigen_source_span){base->source, 73, 74},
		PIGEN_TRANSFER_TYPE_BUF, PIGEN_SEMANTIC_INTERNAL, &e_sym);
	/* T1: d <= s - writes d (PRODUCER), reads the upstream input s (CONSUMER).
	 * The value expression names s. */
	s_e = pigen_expr_add_symbol(m, s_sym, base->t1,
		(pigen_source_span){base->source, 39, 40});
	REQUIRE(!IS_INVALID_ID(s_e));
	dest1 = pigen_lvalue_resolve(m,
		pigen_expr_add_symbol(m, d_sym, base->t1,
			(pigen_source_span){base->source, 56, 57}));
	REQUIRE(!IS_INVALID_ID(dest1));
	*t1 = pigen_transfer_add(m, (pigen_syntax_id){12}, base->module,
		base->process, dest1, s_e, (pigen_predicate_id){0}, base->domain,
		(pigen_transfer_signal_use[]){
			{ s, PIGEN_TRANSFER_CONSUMER },
			{ d, PIGEN_TRANSFER_PRODUCER }
		}, 2, (pigen_source_span){base->source, 39, 57});
	REQUIRE(!IS_INVALID_ID(*t1));
	/* T2: e <= d - writes e (PRODUCER), reads the intermediate d (CONSUMER).
	 * The value expression names d. d is a buffered (or skid) read, so T2
	 * carries the CONSUMER role on d. */
	d_e = pigen_expr_add_symbol(m, d_sym, base->t1,
		(pigen_source_span){base->source, 56, 57});
	REQUIRE(!IS_INVALID_ID(d_e));
	dest2 = pigen_lvalue_resolve(m,
		pigen_expr_add_symbol(m, e_sym, base->t1,
			(pigen_source_span){base->source, 73, 74}));
	REQUIRE(!IS_INVALID_ID(dest2));
	*t2 = pigen_transfer_add(m, (pigen_syntax_id){13}, base->module,
		base->process, dest2, d_e, (pigen_predicate_id){0}, base->domain,
		(pigen_transfer_signal_use[]){
			{ e, PIGEN_TRANSFER_PRODUCER },
			{ d, PIGEN_TRANSFER_CONSUMER }
		}, 2, (pigen_source_span){base->source, 56, 74});
	REQUIRE(!IS_INVALID_ID(*t2));
}

/* sttp_build: build and check the route over the unit's module. A valid
 * route builds and checks with 0, check_valid 1 and an invalid (sentinel)
 * check_span. */
static int sttp_build(const pigen_rtl_lowering *lowering, pigen_route *route,
	pigen_module_id module)
{
	int rc;

	pigen_route_init(route, module);
	rc = pigen_route_build(route, lowering, module);
	if (rc == 0)
		rc = pigen_route_check(route);
	return rc;
}

int main(int argc, char **argv)
{
	pigen_source_manager sources;
	pigen_semantic_model model;
	pigen_rtl_model rtl;
	pigen_rtl_lowering lowering;
	struct sttp_base base;
	pigen_scope_id scope;
	pigen_route route;

	check_init(argc, argv);
	sources = (pigen_source_manager){0};
	rtl = (pigen_rtl_model){0};

	/* ------------------------------------------------------------------ */
	/* (1) Ready propagation: a buffered destination (buf) propagates its  */
	/*     readiness through the ready graph. The two-transfer chain       */
	/*     s -> d -> e (s and e buf, d buf) is built. The transfer T1 that  */
	/*     writes the intermediate buf d must carry the downstream edge      */
	/*     naming T2, the transfer that consumes d: T1's readiness depends   */
	/*     on d draining, and d drains only when its consumer T2 is ready.   */
	/*     T2's destination e has no consumer, so T2 carries no downstream   */
	/*     edge. The current build names no downstream edge (downstream is    */
	/*     the invalid id for every node), so the promised T1->T2 edge is     */
	/*     absent and this section is red until the ready-edge law lands. A  */
	/*     route that over-rejects the legal acyclic buf pipeline fails this  */
	/*     at the build.                                                    */
	/* ------------------------------------------------------------------ */
	SECTION("ready-propagation") {
		pigen_transfer_id t1, t2;
		const pigen_route_ready_node *n1, *n2;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = sttp_base(&model, &sources, &rtl, &lowering,
			"module top : input clk : logic ; input s : buf ; output d : buf ; output e : buf ;\n");
		scope = sttp_scope(&model, base.module);
		sttp_pipeline(&model, scope, &base, PIGEN_TRANSFER_TYPE_BUF, &t1, &t2);
		REQUIRE(sttp_build(&lowering, &route, base.module) == 0);
		REQUIRE(route.check_valid == 1);
		REQUIRE(route.node_count == 2);
		n1 = sttp_node(&route, t1);
		n2 = sttp_node(&route, t2);
		REQUIRE(n1 && n2);
		/* Both destinations are buf (elastic slot): DOWNSTREAM ready. */
		REQUIRE(n1->ready_dependency == sttp_realization_ready(PIGEN_TRANSFER_TYPE_BUF));
		REQUIRE(n2->ready_dependency == sttp_realization_ready(PIGEN_TRANSFER_TYPE_BUF));
		REQUIRE(n1->ready_dependency == PIGEN_TRANSFER_READY_DOWNSTREAM);
		/* T1 (writes the intermediate d) propagates readiness to its consumer
		 * T2: the downstream edge names T2. */
		REQUIRE(n1->downstream.index == t2.index);
		/* T2 (writes the final e, unconsumed) has no downstream edge. */
		REQUIRE(IS_INVALID_ID(n2->downstream));
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (2) Stalled downstream blocks upstream source: the same chain as     */
	/*     (1), pinned from the upstream side. The route's readiness         */
	/*     structure is the whole-unit ready graph: a transfer's readiness    */
	/*     is gated by its downstream edge. T1's readiness depends on T2      */
	/*     (T1.downstream == T2), so a stalled downstream (T2 not ready)      */
	/*     blocks the upstream source T1. This is the backpressure law the    */
	/*     buffer primitive's input-ready (~packet_valid | out_ready)         */
	/*     implements in the emitted SV; the route's ready graph is the       */
	/*     structural witness. Red until the ready-edge law names the         */
	/*     downstream edge.                                                 */
	/* ------------------------------------------------------------------ */
	SECTION("stalled-downstream-blocks-upstream") {
		pigen_transfer_id t1, t2;
		const pigen_route_ready_node *n1;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = sttp_base(&model, &sources, &rtl, &lowering,
			"module top : input clk : logic ; input s : buf ; output d : buf ; output e : buf ;\n");
		scope = sttp_scope(&model, base.module);
		sttp_pipeline(&model, scope, &base, PIGEN_TRANSFER_TYPE_BUF, &t1, &t2);
		REQUIRE(sttp_build(&lowering, &route, base.module) == 0);
		REQUIRE(route.check_valid == 1);
		n1 = sttp_node(&route, t1);
		REQUIRE(n1);
		/* T1's readiness is gated by its downstream edge: when the downstream
		 * (T2) stalls, T1's input-ready falls, so the upstream source is
		 * blocked. The structural witness is the named downstream edge. */
		REQUIRE(n1->ready_dependency == PIGEN_TRANSFER_READY_DOWNSTREAM);
		REQUIRE(n1->downstream.index == t2.index);
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (3) Buffered ready breaks: a skid destination (OCCUPANCY) breaks the  */
	/*     ready chain. The two-transfer chain s -> d -> e is built with d a  */
	/*     SKID (not a buf). The transfer T1 that writes the skid d carries   */
	/*     the OCCUPANCY ready dependency and NO downstream edge: the skid's   */
	/*     readiness depends on its own registered occupancy, not on the       */
	/*     downstream, so the ready chain is broken at d. A skid deliberately  */
	/*     breaks ready chains (it absorbs delayed backpressure). The         */
	/*     downstream side T2 (writes the final buf e, unconsumed) still       */
	/*     carries DOWNSTREAM with no downstream edge. Red until the           */
	/*     ready-edge law distinguishes the OCCUPANCY break from the          */
	/*     DOWNSTREAM propagation: the current build never names a            */
	/*     downstream edge, so it cannot demonstrate the break - this section */
	/*     pins that the OCCUPANCY node carries no downstream edge and the     */
	/*     owner-reported dependency is OCCUPANCY.                             */
	/* ------------------------------------------------------------------ */
	SECTION("buffered-ready-break") {
		pigen_transfer_id t1, t2;
		const pigen_route_ready_node *n1, *n2;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = sttp_base(&model, &sources, &rtl, &lowering,
			"module top : input clk : logic ; input s : buf ; output d : buf ; output e : buf ;\n");
		scope = sttp_scope(&model, base.module);
		sttp_pipeline(&model, scope, &base, PIGEN_TRANSFER_TYPE_SKID, &t1, &t2);
		REQUIRE(sttp_build(&lowering, &route, base.module) == 0);
		REQUIRE(route.check_valid == 1);
		REQUIRE(route.node_count == 2);
		n1 = sttp_node(&route, t1);
		n2 = sttp_node(&route, t2);
		REQUIRE(n1 && n2);
		/* The skid realization is a skid queue with OCCUPANCY ready. */
		REQUIRE(sttp_realization(PIGEN_TRANSFER_TYPE_SKID) ==
			PIGEN_TRANSFER_REALIZATION_SKID_QUEUE);
		REQUIRE(n1->ready_dependency == PIGEN_TRANSFER_READY_OCCUPANCY);
		/* The skid breaks the ready chain: T1 (writes the skid d) carries NO
		 * downstream edge (its readiness is occupancy, not downstream). */
		REQUIRE(IS_INVALID_ID(n1->downstream));
		/* The downstream side still propagates: T2's destination is a buf. */
		REQUIRE(n2->ready_dependency == PIGEN_TRANSFER_READY_DOWNSTREAM);
		REQUIRE(IS_INVALID_ID(n2->downstream));
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (4) Throughput, buffered (buf) ready: the per-cycle transfer and      */
	/*     ready behavior of the buffered realization is pinned against the   */
	/*     owner catalogue. A buf (elastic slot) is DOWNSTREAM, fixed         */
	/*     capacity one, ready propagates (the downstream edge is named); its */
	/*     input-ready is ~packet_valid | out_ready, so one transfer per      */
	/*     cycle when the consumer drains. The route's node carries the        */
	/*     owner-reported DOWNSTREAM dependency and the downstream edge        */
	/*     together. Red until the ready-edge law names the edge.             */
	/* ------------------------------------------------------------------ */
	SECTION("throughput-buffered-ready") {
		pigen_transfer_id t1, t2;
		const pigen_route_ready_node *n1;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = sttp_base(&model, &sources, &rtl, &lowering,
			"module top : input clk : logic ; input s : buf ; output d : buf ; output e : buf ;\n");
		scope = sttp_scope(&model, base.module);
		sttp_pipeline(&model, scope, &base, PIGEN_TRANSFER_TYPE_BUF, &t1, &t2);
		REQUIRE(sttp_build(&lowering, &route, base.module) == 0);
		REQUIRE(route.check_valid == 1);
		n1 = sttp_node(&route, t1);
		REQUIRE(n1);
		/* The owner catalogue pins the buf's per-cycle ready behavior: a
		 * buffered elastic slot, DOWNSTREAM ready, fixed capacity one. */
		REQUIRE(sttp_realization(PIGEN_TRANSFER_TYPE_BUF) ==
			PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT);
		REQUIRE(sttp_capacity(PIGEN_TRANSFER_TYPE_BUF) == 1);
		REQUIRE(sttp_realization_ready(PIGEN_TRANSFER_TYPE_BUF) ==
			PIGEN_TRANSFER_READY_DOWNSTREAM);
		/* The route's node carries the owner-reported DOWNSTREAM dependency
		 * and the downstream edge together. */
		REQUIRE(n1->ready_dependency == PIGEN_TRANSFER_READY_DOWNSTREAM);
		REQUIRE(n1->downstream.index == t2.index);
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (5) Throughput, skid ready: the per-cycle transfer and ready behavior  */
	/*     of the skid realization is pinned against the owner catalogue. A   */
	/*     skid is a two-entry storage with registered-occupancy nonfull       */
	/*     readiness, fixed capacity two, OCCUPANCY ready (ready chain        */
	/*     broken); it absorbs delayed backpressure and supports simultaneous  */
	/*     push/pop while nonfull. The route's node carries the owner-         */
	/*     reported OCCUPANCY dependency and no downstream edge together.     */
	/* ------------------------------------------------------------------ */
	SECTION("throughput-skid-ready") {
		pigen_transfer_id t1, t2;
		const pigen_route_ready_node *n1;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = sttp_base(&model, &sources, &rtl, &lowering,
			"module top : input clk : logic ; input s : buf ; output d : buf ; output e : buf ;\n");
		scope = sttp_scope(&model, base.module);
		sttp_pipeline(&model, scope, &base, PIGEN_TRANSFER_TYPE_SKID, &t1, &t2);
		REQUIRE(sttp_build(&lowering, &route, base.module) == 0);
		REQUIRE(route.check_valid == 1);
		n1 = sttp_node(&route, t1);
		REQUIRE(n1);
		/* The owner catalogue pins the skid's per-cycle ready behavior: a
		 * skid queue, OCCUPANCY ready, fixed capacity two (two-entry storage
		 * with registered-occupancy nonfull readiness). */
		REQUIRE(sttp_realization(PIGEN_TRANSFER_TYPE_SKID) ==
			PIGEN_TRANSFER_REALIZATION_SKID_QUEUE);
		REQUIRE(sttp_capacity(PIGEN_TRANSFER_TYPE_SKID) == 2);
		REQUIRE(sttp_realization_ready(PIGEN_TRANSFER_TYPE_SKID) ==
			PIGEN_TRANSFER_READY_OCCUPANCY);
		/* The route's node carries the owner-reported OCCUPANCY dependency
		 * and no downstream edge together (the skid breaks the ready chain). */
		REQUIRE(n1->ready_dependency == PIGEN_TRANSFER_READY_OCCUPANCY);
		REQUIRE(IS_INVALID_ID(n1->downstream));
		pigen_route_free(&route);
	}

	return check_finish();
}
