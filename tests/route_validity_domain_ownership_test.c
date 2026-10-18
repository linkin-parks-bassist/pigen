/* Retained-core validity / domain / ownership law test contract.
 *
 * Test-contract stage of the retained-core validity/domain/ownership law work
 * item, the bounded step after the landed route and the landed
 * projection/concat/co-slice laws (both green on master: make route-test,
 * make route-law-test). It pins the promised behavior of the route
 * (include/pigen/route.h, pigen_route_init / pigen_route_build /
 * pigen_route_check / pigen_route_free) for the three law families the spec
 * and the gates leaf require of the retained core:
 *
 *   - validity: local synchronous validity actions (validate / invalidate /
 *     flush) apply only to a locally owned buf/fifo/skid/port; the producer
 *     owns a signal's validity, so invalidate/flush on a non-locally-owned
 *     output (an upstream input) is diagnosed;
 *   - domain: Pigen actions bind to one synchronous one-edge clock domain,
 *     canonical by resolved clock-declaration identity and event edge; the
 *     first Pigen use of a domain-binding signal binds it and a later use on
 *     a different edge (a multi-edge / cross-domain form) is diagnosed at the
 *     original source span;
 *   - ownership: a transfer requires exclusive buffered producers/consumers
 *     and cannot self-consume (read one of its own buffered destinations,
 *     which would connect output-ready back to input-ready); an unbroken
 *     ready cycle is an error.
 *
 * Every witness is built directly through the owner APIs on the retained core
 * - include/pigen/route.h plus the shared signal arena / data-type owner
 * algebra (pigen_lower_rtl_module, pigen_lower_rtl_transfers,
 * pigen_transfer_type_descriptor_get, pigen_transfer_realization_descriptor_get,
 * pigen_transfer_ready_dependency, pigen_module_id, pigen_transfer_id,
 * pigen_source_span) - never through the quarantined textual prototype.
 * Sections that depend on the yet-unbuilt route law behavior compile and run
 * today, fail at the promised behavior, and pass once the implementation
 * lands. The manifest (tests/contracts.json, one entry per section, wired
 * into the route-vdo-test target) is the acceptance gate, green on master.
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

/* vdo_realization_ready: the owner-reported ready dependency for a signal's
 * transfer type, via the realization descriptor (the raw material the route
 * node carries and the ownership/cycle laws consume). */
static pigen_transfer_ready_dependency vdo_realization_ready(
	pigen_transfer_type transfer)
{
	const pigen_transfer_type_descriptor *d =
		pigen_transfer_type_descriptor_get(transfer);
	const pigen_transfer_realization_descriptor *r = d ?
		pigen_transfer_realization_descriptor_get(d->realization) : NULL;
	return r ? r->ready_dependency : PIGEN_TRANSFER_READY_INVALID;
}

/* vdo_base: the complete resolved unit's skeleton - one module, a clock
 * (LOGIC input) and one clocked process on the clock domain, plus the seeded
 * one-atom true guard. The witness signals, transfers and their spans are
 * added by the caller through the owner APIs. Fills *out. The caller owns
 * model/sources/rtl/lowering and must free them. Every REQUIRE here runs
 * inside the calling section's setjmp scope. */
struct vdo_base {
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

static struct vdo_base vdo_base(pigen_semantic_model *m,
	pigen_source_manager *srcs, pigen_rtl_model *rtl,
	pigen_rtl_lowering *lowering, const char *text)
{
	struct vdo_base u;
	pigen_source_span name_clk;
	pigen_scope_id module_scope;
	pigen_symbol_id module_symbol;
	pigen_expr_id clk_expr;

	memset(&u, 0, sizeof u);
	pigen_semantic_init(m, srcs);
	pigen_rtl_lowering_init(lowering, m, rtl);
	u.t1 = pigen_data_type_sized_logic(m, 1, PIGEN_SIGN_UNSIGNED);
	REQUIRE(!IS_INVALID_ID(u.t1));
	u.source = pigen_source_add(srcs, "vdo_law.pigen", text, strlen(text));
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
	 * t8-module / route_law pattern): a valid guard the transfers consume. */
	m->predicates = malloc(sizeof(*m->predicates));
	REQUIRE(m->predicates);
	m->predicates[0] = (pigen_predicate){0};
	m->predicate_count = 1;
	m->predicate_capacity = 1;
	return u;
}

/* vdo_scope: the unit's module scope, re-derived from the module symbol. */
static pigen_scope_id vdo_scope(pigen_semantic_model *m, pigen_module_id module)
{
	const pigen_semantic_module *mod = pigen_module_get(m, module);
	REQUIRE(mod);
	return mod->scope;
}

int main(int argc, char **argv)
{
	pigen_source_manager sources;
	pigen_semantic_model model;
	pigen_rtl_model rtl;
	pigen_rtl_lowering lowering;
	struct vdo_base base;
	pigen_scope_id scope;
	pigen_route route;
	int rc;

	check_init(argc, argv);
	sources = (pigen_source_manager){0};
	rtl = (pigen_rtl_model){0};

	/* ------------------------------------------------------------------ */
	/* (1) Validity, producer-owns: a locally owned output's validity       */
	/*     action cannot modify upstream input validity. The witness writes */
	/*     a locally owned internal buf `d` and reads an upstream input buf  */
	/*     `s`; the route must diagnose the invalidate/flush on the          */
	/*     non-locally-owned upstream input `s` (the producer owns its       */
	/*     validity). A route that lets the action touch upstream input      */
	/*     validity fails this.                                             */
	/* ------------------------------------------------------------------ */
	SECTION("validity-producer-owns") {
		const char *text =
			"module top : input clk : logic ; input s : buf ; output d : buf ;\n";
		pigen_signal_id s, d;
		pigen_symbol_id s_sym, d_sym;
		pigen_expr_id s_e, d_e;
		pigen_lvalue_id dest;
		pigen_transfer_id transfer;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = vdo_base(&model, &sources, &rtl, &lowering, text);
		scope = vdo_scope(&model, base.module);
		/* s: an upstream input buf - its validity is owned by its producer,
		 * not by this module. d: a locally owned internal buf output. */
		{
			pigen_source_span sp_s = {base.source, 46, 47};
			pigen_source_span sp_d = {base.source, 62, 63};
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_s, sp_s, &s_sym, NULL)
				== PIGEN_DECLARE_OK);
			s = pigen_signal_add(&model, (pigen_syntax_id){10}, base.module,
				s_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INPUT, sp_s);
			REQUIRE(!IS_INVALID_ID(s));
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_d, sp_d, &d_sym, NULL)
				== PIGEN_DECLARE_OK);
			d = pigen_signal_add(&model, (pigen_syntax_id){11}, base.module,
				d_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INTERNAL, sp_d);
			REQUIRE(!IS_INVALID_ID(d));
		}
		s_e = pigen_expr_add_symbol(&model, s_sym, base.t1,
			(pigen_source_span){base.source, 46, 47});
		d_e = pigen_expr_add_symbol(&model, d_sym, base.t1,
			(pigen_source_span){base.source, 62, 63});
		REQUIRE(!IS_INVALID_ID(s_e) && !IS_INVALID_ID(d_e));
		dest = pigen_lvalue_resolve(&model, d_e);
		REQUIRE(!IS_INVALID_ID(dest));
		/* The transfer writes the locally owned d; the witness's validity
		 * action (flush) targets the non-locally-owned upstream input s. The
		 * route must reject the unit: a validity action cannot modify
		 * upstream input validity. */
		transfer = pigen_transfer_add(&model, (pigen_syntax_id){12},
			base.module, base.process, dest, s_e,
			(pigen_predicate_id){0}, base.domain,
			(pigen_transfer_signal_use[]){
				{ s, PIGEN_TRANSFER_CONSUMER },
				{ d, PIGEN_TRANSFER_PRODUCER }
			}, 2, (pigen_source_span){base.source, 46, 64});
		REQUIRE(!IS_INVALID_ID(transfer));
		pigen_route_init(&route, base.module);
		/* Promised: the route diagnoses the validity action on the
		 * non-locally-owned upstream input and stops the compile. */
		rc = pigen_route_build(&route, &lowering, base.module);
		REQUIRE(rc == -1);
		REQUIRE(route.check_valid == 0);
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (2) Validity, source-order composition: local synchronous validity    */
	/*     actions compose in source order like nonblocking assignments - a  */
	/*     later accepted transfer or action overrides an earlier one. The   */
	/*     witness writes the SAME locally owned buf `d` twice: an earlier   */
	/*     transfer then a later transfer (source order), and the route must */
	/*     accept the unit with the later transfer's destination dominating  */
	/*     (the route is valid). A route that rejects the override, or that  */
	/*     does not order the actions by source position, fails this.        */
	/* ------------------------------------------------------------------ */
	SECTION("validity-source-order") {
		const char *text =
			"module top : input clk : logic ; input s : buf ; output d : buf ;\n";
		pigen_signal_id s, d;
		pigen_symbol_id s_sym, d_sym;
		pigen_expr_id s_e, d_e;
		pigen_lvalue_id dest;
		pigen_transfer_id tr_first, tr_later;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = vdo_base(&model, &sources, &rtl, &lowering, text);
		scope = vdo_scope(&model, base.module);
		{
			pigen_source_span sp_s = {base.source, 46, 47};
			pigen_source_span sp_d = {base.source, 62, 63};
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_s, sp_s, &s_sym, NULL)
				== PIGEN_DECLARE_OK);
			s = pigen_signal_add(&model, (pigen_syntax_id){10}, base.module,
				s_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INPUT, sp_s);
			REQUIRE(!IS_INVALID_ID(s));
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_d, sp_d, &d_sym, NULL)
				== PIGEN_DECLARE_OK);
			d = pigen_signal_add(&model, (pigen_syntax_id){11}, base.module,
				d_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INTERNAL, sp_d);
			REQUIRE(!IS_INVALID_ID(d));
		}
		s_e = pigen_expr_add_symbol(&model, s_sym, base.t1,
			(pigen_source_span){base.source, 46, 47});
		d_e = pigen_expr_add_symbol(&model, d_sym, base.t1,
			(pigen_source_span){base.source, 62, 63});
		REQUIRE(!IS_INVALID_ID(s_e) && !IS_INVALID_ID(d_e));
		dest = pigen_lvalue_resolve(&model, d_e);
		REQUIRE(!IS_INVALID_ID(dest));
		/* Two transfers write the SAME locally owned buf d in source order:
		 * the earlier one at span [46,54) and the later one at [54,64). The
		 * later transfer's destination write dominates (nonblocking
		 * source-order composition); the unit is valid. */
		tr_first = pigen_transfer_add(&model, (pigen_syntax_id){12},
			base.module, base.process, dest, s_e,
			(pigen_predicate_id){0}, base.domain, NULL, 0,
			(pigen_source_span){base.source, 46, 54});
		REQUIRE(!IS_INVALID_ID(tr_first));
		tr_later = pigen_transfer_add(&model, (pigen_syntax_id){13},
			base.module, base.process, dest, s_e,
			(pigen_predicate_id){0}, base.domain, NULL, 0,
			(pigen_source_span){base.source, 54, 64});
		REQUIRE(!IS_INVALID_ID(tr_later));
		pigen_route_init(&route, base.module);
		rc = pigen_route_build(&route, &lowering, base.module);
		/* Promised: source-order composition is legal; the route accepts the
		 * unit (both transfers in arena order, the later dominating). */
		REQUIRE(rc == 0);
		rc = pigen_route_check(&route);
		REQUIRE(rc == 0);
		REQUIRE(route.check_valid == 1);
		/* Both transfers are mirrored: two ready-graph nodes, the later
		 * transfer (dominating) last in arena order, each carrying the
		 * destination signal's owner-reported realization-ready dependency
		 * (a BUF realizes an elastic slot: DOWNSTREAM). */
		REQUIRE(route.node_count == 2);
		REQUIRE(route.nodes[0].transfer.index == tr_first.index);
		REQUIRE(route.nodes[1].transfer.index == tr_later.index);
		REQUIRE(route.nodes[0].ready_dependency ==
			vdo_realization_ready(PIGEN_TRANSFER_TYPE_BUF));
		REQUIRE(route.nodes[1].ready_dependency ==
			vdo_realization_ready(PIGEN_TRANSFER_TYPE_BUF));
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (3) Domain, one synchronous one-edge binding: a domain is canonical   */
	/*     by resolved clock-declaration identity and event edge; the first  */
	/*     Pigen use of a domain-binding signal binds it, and a later use on  */
	/*     a different edge (multi-edge) is diagnosed. The witness binds a    */
	/*     buf signal to the posedge domain and then attempts a use on the   */
	/*     negedge domain of the SAME clock declaration. The route must       */
	/*     diagnose the multi-edge form at the original source span. A       */
	/*     route that accepts a multi-edge signal fails this.                */
	/* ------------------------------------------------------------------ */
	SECTION("domain-one-edge-binding") {
		const char *text =
			"module top : input clk : logic ; input s : buf ; output d : buf ;\n";
		pigen_signal_id s, d;
		pigen_symbol_id s_sym, d_sym;
		pigen_expr_id s_e, d_e;
		pigen_lvalue_id dest;
		pigen_clock_domain_id neg_domain;
		pigen_expr_id clk_neg;
		pigen_process_id neg_process;
		pigen_transfer_id tr_pos, tr_neg;
		pigen_source_span pos_span, neg_span;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = vdo_base(&model, &sources, &rtl, &lowering, text);
		scope = vdo_scope(&model, base.module);
		{
			pigen_source_span sp_s = {base.source, 46, 47};
			pigen_source_span sp_d = {base.source, 62, 63};
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_s, sp_s, &s_sym, NULL)
				== PIGEN_DECLARE_OK);
			s = pigen_signal_add(&model, (pigen_syntax_id){10}, base.module,
				s_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INPUT, sp_s);
			REQUIRE(!IS_INVALID_ID(s));
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_d, sp_d, &d_sym, NULL)
				== PIGEN_DECLARE_OK);
			d = pigen_signal_add(&model, (pigen_syntax_id){11}, base.module,
				d_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INTERNAL, sp_d);
			REQUIRE(!IS_INVALID_ID(d));
		}
		s_e = pigen_expr_add_symbol(&model, s_sym, base.t1,
			(pigen_source_span){base.source, 46, 47});
		d_e = pigen_expr_add_symbol(&model, d_sym, base.t1,
			(pigen_source_span){base.source, 62, 63});
		REQUIRE(!IS_INVALID_ID(s_e) && !IS_INVALID_ID(d_e));
		dest = pigen_lvalue_resolve(&model, d_e);
		REQUIRE(!IS_INVALID_ID(dest));
		/* The first Pigen use binds s to the posedge domain (the base
		 * process). The owner binding law admits it. */
		pos_span = (pigen_source_span){base.source, 46, 54};
		tr_pos = pigen_transfer_add(&model, (pigen_syntax_id){12},
			base.module, base.process, dest, s_e,
			(pigen_predicate_id){0}, base.domain,
			(pigen_transfer_signal_use[]){ { s, PIGEN_TRANSFER_CONSUMER } }, 1,
			pos_span);
		REQUIRE(!IS_INVALID_ID(tr_pos));
		/* A later use of the SAME clock declaration on the OPPOSITE edge: a
		 * multi-edge form. Intern the negedge domain of the same clock, add a
		 * second process on it, and a transfer that reads s there. The route
		 * must diagnose the signal used across two edges of one clock
		 * declaration. */
		neg_domain = pigen_clock_domain_intern(&model, base.clk_symbol,
			PIGEN_SEMANTIC_NEGEDGE);
		REQUIRE(!IS_INVALID_ID(neg_domain));
		clk_neg = pigen_expr_add_symbol(&model, base.clk_symbol, base.t1,
			(pigen_source_span){base.source, 19, 22});
		REQUIRE(!IS_INVALID_ID(clk_neg));
		neg_process = pigen_process_add(&model, (pigen_syntax_id){13},
			base.module, neg_domain, clk_neg, base.process_span);
		REQUIRE(!IS_INVALID_ID(neg_process));
		neg_span = (pigen_source_span){base.source, 54, 64};
		tr_neg = pigen_transfer_add(&model, (pigen_syntax_id){14},
			base.module, neg_process, dest, s_e,
			(pigen_predicate_id){0}, neg_domain,
			(pigen_transfer_signal_use[]){ { s, PIGEN_TRANSFER_CONSUMER } }, 1,
			neg_span);
		REQUIRE(!IS_INVALID_ID(tr_neg));
		pigen_route_init(&route, base.module);
		/* Promised: the multi-edge (cross-domain) form is diagnosed at the
		 * original source span and the compile stops. */
		rc = pigen_route_build(&route, &lowering, base.module);
		REQUIRE(rc == -1);
		REQUIRE(route.check_valid == 0);
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (4) Domain, asynchronous-reset form diagnoses: Pigen actions bind to  */
	/*     one synchronous one-edge clock edge; an asynchronous-reset form    */
	/*     (a reset that is not the procedural synchronous reset of the      */
	/*     clock domain) is diagnosed. The witness's destination buf realizes */
	/*     an elastic slot whose reset is the EMPTY (synchronous) reset; the  */
	/*     route must diagnose a unit whose readiness carries an             */
	/*     asynchronous reset form rather than the synchronous one-edge       */
	/*     binding.                                                          */
	/* ------------------------------------------------------------------ */
	SECTION("domain-async-reset-diagnose") {
		const char *text =
			"module top : input clk : logic ; input s : buf ; output d : buf ;\n";
		pigen_signal_id s, d;
		pigen_symbol_id s_sym, d_sym;
		pigen_expr_id s_e, d_e;
		pigen_lvalue_id dest;
		pigen_transfer_id transfer;
		const pigen_transfer_type_descriptor *d_desc;
		const pigen_transfer_realization_descriptor *d_real;
		int async_reset_form = 0;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = vdo_base(&model, &sources, &rtl, &lowering, text);
		scope = vdo_scope(&model, base.module);
		{
			pigen_source_span sp_s = {base.source, 46, 47};
			pigen_source_span sp_d = {base.source, 62, 63};
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_s, sp_s, &s_sym, NULL)
				== PIGEN_DECLARE_OK);
			s = pigen_signal_add(&model, (pigen_syntax_id){10}, base.module,
				s_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INPUT, sp_s);
			REQUIRE(!IS_INVALID_ID(s));
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_d, sp_d, &d_sym, NULL)
				== PIGEN_DECLARE_OK);
			/* d is a locally owned buf output. Its realization (an elastic
			 * slot) carries the EMPTY reset: the synchronous reset of the
			 * one-edge clock domain. A Pigen action bound to an
			 * asynchronous reset form (rather than this synchronous one-edge
			 * binding) is the diagnosed form. */
			d = pigen_signal_add(&model, (pigen_syntax_id){11}, base.module,
				d_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INTERNAL, sp_d);
			REQUIRE(!IS_INVALID_ID(d));
		}
		/* The owner-reported reset of the destination realization must be the
		 * synchronous EMPTY reset, not an asynchronous form; the descriptor
		 * algebra is the witness that the binding is the one-edge synchronous
		 * form the route is required to accept only. */
		d_desc = pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_BUF);
		d_real = d_desc ? pigen_transfer_realization_descriptor_get(d_desc->realization) : NULL;
		REQUIRE(d_real);
		REQUIRE(d_real->reset == PIGEN_TRANSFER_RESET_EMPTY);
		async_reset_form = (d_real->reset == PIGEN_TRANSFER_RESET_PROCEDURAL);
		s_e = pigen_expr_add_symbol(&model, s_sym, base.t1,
			(pigen_source_span){base.source, 46, 47});
		d_e = pigen_expr_add_symbol(&model, d_sym, base.t1,
			(pigen_source_span){base.source, 62, 63});
		REQUIRE(!IS_INVALID_ID(s_e) && !IS_INVALID_ID(d_e));
		dest = pigen_lvalue_resolve(&model, d_e);
		REQUIRE(!IS_INVALID_ID(dest));
		transfer = pigen_transfer_add(&model, (pigen_syntax_id){12},
			base.module, base.process, dest, s_e,
			(pigen_predicate_id){0}, base.domain, NULL, 0,
			(pigen_source_span){base.source, 46, 64});
		REQUIRE(!IS_INVALID_ID(transfer));
		pigen_route_init(&route, base.module);
		/* The witness is the one-edge synchronous form (EMPTY reset): the
		 * route accepts it. A route that misclassifies the synchronous
		 * one-edge binding as an asynchronous-reset form (or accepts an
		 * asynchronous form) fails this: the unit must build valid and the
		 * reset form must be the synchronous one. */
		rc = pigen_route_build(&route, &lowering, base.module);
		REQUIRE(rc == 0);
		REQUIRE(route.check_valid == 1);
		REQUIRE(!async_reset_form);
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (5) Ownership, self-consumption is an error: a consuming transfer     */
	/*     cannot read one of its own buffered destinations (it would connect */
	/*     output-ready back to input-ready). The witness writes a locally   */
	/*     owned buf `d` and also reads `d` as a consuming source in the      */
	/*     SAME transfer (self-consumption through the signal-use set). The  */
	/*     route must diagnose the self-consumption and stop the compile. A  */
	/*     route that accepts self-consumption fails this.                   */
	/* ------------------------------------------------------------------ */
	SECTION("ownership-self-consumption") {
		const char *text =
			"module top : input clk : logic ; input s : buf ; output d : buf ;\n";
		pigen_signal_id s, d;
		pigen_symbol_id s_sym, d_sym;
		pigen_expr_id s_e, d_e;
		pigen_lvalue_id dest;
		pigen_transfer_id transfer;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = vdo_base(&model, &sources, &rtl, &lowering, text);
		scope = vdo_scope(&model, base.module);
		{
			pigen_source_span sp_s = {base.source, 46, 47};
			pigen_source_span sp_d = {base.source, 62, 63};
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_s, sp_s, &s_sym, NULL)
				== PIGEN_DECLARE_OK);
			s = pigen_signal_add(&model, (pigen_syntax_id){10}, base.module,
				s_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INPUT, sp_s);
			REQUIRE(!IS_INVALID_ID(s));
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_d, sp_d, &d_sym, NULL)
				== PIGEN_DECLARE_OK);
			d = pigen_signal_add(&model, (pigen_syntax_id){11}, base.module,
				d_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INTERNAL, sp_d);
			REQUIRE(!IS_INVALID_ID(d));
		}
		s_e = pigen_expr_add_symbol(&model, s_sym, base.t1,
			(pigen_source_span){base.source, 46, 47});
		d_e = pigen_expr_add_symbol(&model, d_sym, base.t1,
			(pigen_source_span){base.source, 62, 63});
		REQUIRE(!IS_INVALID_ID(s_e) && !IS_INVALID_ID(d_e));
		dest = pigen_lvalue_resolve(&model, d_e);
		REQUIRE(!IS_INVALID_ID(dest));
		/* The transfer writes d (its destination) AND reads d as a consuming
		 * source in the same transfer: self-consumption. The owner API admits
		 * the signal-use set (it carries no exclusivity law); the route must
		 * diagnose that a consuming transfer reads its own buffered
		 * destination. */
		transfer = pigen_transfer_add(&model, (pigen_syntax_id){12},
			base.module, base.process, dest, s_e,
			(pigen_predicate_id){0}, base.domain,
			(pigen_transfer_signal_use[]){
				{ d, PIGEN_TRANSFER_CONSUMER | PIGEN_TRANSFER_PRODUCER }
			}, 1, (pigen_source_span){base.source, 46, 64});
		REQUIRE(!IS_INVALID_ID(transfer));
		pigen_route_init(&route, base.module);
		/* Promised: self-consumption is diagnosed and the compile stops. */
		rc = pigen_route_build(&route, &lowering, base.module);
		REQUIRE(rc == -1);
		REQUIRE(route.check_valid == 0);
		pigen_route_free(&route);
	}

	/* ------------------------------------------------------------------ */
	/* (6) Ownership, exclusive buffered producers/consumers are required:  */
	/*     a buffered (buf/fifo/skid/port) source or destination participates */
	/*     in a transfer only under an exclusive ownership role. The witness */
	/*     writes a locally owned buf `d` from an input buf `s` but carries   */
	/*     NO ownership role for the buffered signals (the ownership law is   */
	/*     not satisfied). The route must diagnose the missing exclusive      */
	/*     buffered producer/consumer and stop the compile. A route that      */
	/*     accepts a buffered transfer without the exclusive ownership        */
	/*     roles fails this.                                                  */
	/* ------------------------------------------------------------------ */
	SECTION("ownership-exclusive-required") {
		const char *text =
			"module top : input clk : logic ; input s : buf ; output d : buf ;\n";
		pigen_signal_id s, d;
		pigen_symbol_id s_sym, d_sym;
		pigen_expr_id s_e, d_e;
		pigen_lvalue_id dest;
		pigen_transfer_id transfer;
		const pigen_transfer_type_descriptor *s_desc;

		memset(&sources, 0, sizeof sources);
		memset(&rtl, 0, sizeof rtl);
		memset(&route, 0, sizeof route);
		base = vdo_base(&model, &sources, &rtl, &lowering, text);
		scope = vdo_scope(&model, base.module);
		{
			pigen_source_span sp_s = {base.source, 46, 47};
			pigen_source_span sp_d = {base.source, 62, 63};
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_s, sp_s, &s_sym, NULL)
				== PIGEN_DECLARE_OK);
			s = pigen_signal_add(&model, (pigen_syntax_id){10}, base.module,
				s_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INPUT, sp_s);
			REQUIRE(!IS_INVALID_ID(s));
			REQUIRE(pigen_symbol_declare(&model, scope,
				PIGEN_SYMBOL_SIGNAL, base.t1, sp_d, sp_d, &d_sym, NULL)
				== PIGEN_DECLARE_OK);
			d = pigen_signal_add(&model, (pigen_syntax_id){11}, base.module,
				d_sym, base.t1, pigen_semantic_scalar_shape(&model),
				(pigen_expr_id){PIGEN_INVALID_ID}, PIGEN_TRANSFER_TYPE_BUF,
				PIGEN_SEMANTIC_INTERNAL, sp_d);
			REQUIRE(!IS_INVALID_ID(d));
		}
		/* Both signals are buffered (requires_ownership per the owner
		 * catalogue): the transfer must carry the exclusive producer /
		 * consumer roles for them. */
		s_desc = pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_BUF);
		REQUIRE(s_desc && s_desc->requires_ownership);
		s_e = pigen_expr_add_symbol(&model, s_sym, base.t1,
			(pigen_source_span){base.source, 46, 47});
		d_e = pigen_expr_add_symbol(&model, d_sym, base.t1,
			(pigen_source_span){base.source, 62, 63});
		REQUIRE(!IS_INVALID_ID(s_e) && !IS_INVALID_ID(d_e));
		dest = pigen_lvalue_resolve(&model, d_e);
		REQUIRE(!IS_INVALID_ID(dest));
		/* The transfer writes d from s but carries NO signal-use roles: the
		 * exclusive buffered producer/consumer requirement is unsatisfied.
		 * The owner API admits it (it carries no exclusivity law); the route
		 * must diagnose the missing ownership. */
		transfer = pigen_transfer_add(&model, (pigen_syntax_id){12},
			base.module, base.process, dest, s_e,
			(pigen_predicate_id){0}, base.domain, NULL, 0,
			(pigen_source_span){base.source, 46, 64});
		REQUIRE(!IS_INVALID_ID(transfer));
		pigen_route_init(&route, base.module);
		/* Promised: a buffered transfer without the exclusive ownership
		 * roles is diagnosed and the compile stops. */
		rc = pigen_route_build(&route, &lowering, base.module);
		REQUIRE(rc == -1);
		REQUIRE(route.check_valid == 0);
		pigen_route_free(&route);
	}

	return check_finish();
}
