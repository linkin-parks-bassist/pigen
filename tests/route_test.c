/* Retained-core route test contract.

 * Test-contract stage of the retained-core route work item. It pins the
 * promised behavior of pigen_route_init / pigen_route_build /
 * pigen_route_check / pigen_route_free (include/pigen/route.h) for the
 * minimal resolved unit - a module whose declarations, one clocked process
 * and one direct atomic transfer resolve - plus the whole-unit ready graph
 * (one node per direct atomic transfer, in arena order, each carrying the
 * destination signal's realization-ready dependency), the module
 * isolation, and the sizing/signedness/aggregate check result.
 *
 * Every section builds its witness directly through the owner APIs (the
 * landed construction pattern of the rtl_lower_test t8 sections), never
 * through the prototype dialect. Sections that depend on the yet-unbuilt
 * route build/check behavior compile and run today; they fail at the
 * promised behavior and pass once the implementation lands. The manifest
 * (tests/contracts.json, one entry per section) is the acceptance gate:
 * every command green on master.
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

/* rw_realization_ready: the owner-reported ready dependency for a signal's
 * transfer type, via the realization descriptor (the raw material the route
 * node must carry). */
static pigen_transfer_ready_dependency rw_realization_ready(
	pigen_transfer_type transfer)
{
	const pigen_transfer_type_descriptor *d =
		pigen_transfer_type_descriptor_get(transfer);
	const pigen_transfer_realization_descriptor *r = d ?
		pigen_transfer_realization_descriptor_get(d->realization) : NULL;
	return r ? r->ready_dependency : PIGEN_TRANSFER_READY_INVALID;
}

/* rw_unit: the complete minimal resolved unit - one module, a clock (LOGIC
 * input), one BUF source s (input), one BUF destination d (internal), one
 * clocked process on the clock domain, and one transfer d <= s guarded by
 * the seeded one-atom true predicate. The t8-ownership witness, verbatim in
 * its owner calls. Fills *out with the identities and spans the assertions
 * ride on. The caller owns model/sources/rtl/lowering and must free them. */
struct rw_unit {
	pigen_module_id module;
	pigen_signal_id s;
	pigen_signal_id d;
	pigen_clock_domain_id domain;
	pigen_process_id process;
	pigen_transfer_id transfer;
	pigen_source_span transfer_span;
};

static struct rw_unit rw_unit(pigen_semantic_model *m,
	pigen_source_manager *srcs, pigen_rtl_model *rtl,
	pigen_rtl_lowering *lowering)
{
	struct rw_unit u;
	const char *text =
		"module top : input clk : logic ; input s : buf ; output d : buf ;\n";
	pigen_data_type_id t1;
	pigen_source_id source;
	pigen_source_span whole, name_clk, name_s, name_d, process_span;
	pigen_scope_id module_scope;
	pigen_symbol_id module_symbol, clk_symbol, s_symbol, d_symbol;
	pigen_signal_id clk;
	pigen_expr_id clk_expr, s_expr, d_expr;
	pigen_lvalue_id dest;
	pigen_transfer_id transfer;

	memset(&u, 0, sizeof u);
	pigen_semantic_init(m, srcs);
	pigen_rtl_lowering_init(lowering, m, rtl);
	t1 = pigen_data_type_sized_logic(m, 1, PIGEN_SIGN_UNSIGNED);
	REQUIRE(!IS_INVALID_ID(t1));
	source = pigen_source_add(srcs, "route_contract.pigen", text,
		strlen(text));
	REQUIRE(source.index != PIGEN_INVALID_ID);
	whole = (pigen_source_span){source, 0, strlen(text)};
	name_clk = (pigen_source_span){source, 19, 22}; /* "clk" */
	name_s = (pigen_source_span){source, 46, 47}; /* "s" */
	name_d = (pigen_source_span){source, 62, 63}; /* "d" */
	process_span = (pigen_source_span){source, 13, 64};
	m->compilation_scope = pigen_scope_add(m,
		(pigen_scope_id){PIGEN_INVALID_ID},
		(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0});
	REQUIRE(!IS_INVALID_ID(m->compilation_scope));
	REQUIRE(pigen_symbol_declare(m, m->compilation_scope, PIGEN_SYMBOL_MODULE,
		(pigen_data_type_id){PIGEN_INVALID_ID}, whole, whole,
		&module_symbol, NULL) == PIGEN_DECLARE_OK);
	module_scope = pigen_scope_add(m, m->compilation_scope, whole);
	REQUIRE(!IS_INVALID_ID(module_scope));
	u.module = pigen_module_add(m, (pigen_syntax_id){1}, module_symbol,
		module_scope, whole);
	REQUIRE(!IS_INVALID_ID(u.module));
	REQUIRE(pigen_symbol_declare(m, module_scope, PIGEN_SYMBOL_SIGNAL, t1,
		name_clk, name_clk, &clk_symbol, NULL) == PIGEN_DECLARE_OK);
	clk = pigen_signal_add(m, (pigen_syntax_id){2}, u.module, clk_symbol, t1,
		pigen_semantic_scalar_shape(m), (pigen_expr_id){PIGEN_INVALID_ID},
		PIGEN_TRANSFER_TYPE_LOGIC, PIGEN_SEMANTIC_INPUT, name_clk);
	REQUIRE(!IS_INVALID_ID(clk));
	REQUIRE(pigen_symbol_declare(m, module_scope, PIGEN_SYMBOL_SIGNAL, t1,
		name_s, name_s, &s_symbol, NULL) == PIGEN_DECLARE_OK);
	u.s = pigen_signal_add(m, (pigen_syntax_id){3}, u.module, s_symbol, t1,
		pigen_semantic_scalar_shape(m), (pigen_expr_id){PIGEN_INVALID_ID},
		PIGEN_TRANSFER_TYPE_BUF, PIGEN_SEMANTIC_INPUT, name_s);
	REQUIRE(!IS_INVALID_ID(u.s));
	REQUIRE(pigen_symbol_declare(m, module_scope, PIGEN_SYMBOL_SIGNAL, t1,
		name_d, name_d, &d_symbol, NULL) == PIGEN_DECLARE_OK);
	u.d = pigen_signal_add(m, (pigen_syntax_id){4}, u.module, d_symbol, t1,
		pigen_semantic_scalar_shape(m), (pigen_expr_id){PIGEN_INVALID_ID},
		PIGEN_TRANSFER_TYPE_BUF, PIGEN_SEMANTIC_INTERNAL, name_d);
	REQUIRE(!IS_INVALID_ID(u.d));
	u.domain = pigen_clock_domain_intern(m, clk_symbol,
		PIGEN_SEMANTIC_POSEDGE);
	REQUIRE(!IS_INVALID_ID(u.domain));
	clk_expr = pigen_expr_add_symbol(m, clk_symbol, t1, name_clk);
	REQUIRE(!IS_INVALID_ID(clk_expr));
	u.process = pigen_process_add(m, (pigen_syntax_id){5}, u.module,
		u.domain, clk_expr, process_span);
	REQUIRE(!IS_INVALID_ID(u.process));
	s_expr = pigen_expr_add_symbol(m, s_symbol, t1, name_s);
	REQUIRE(!IS_INVALID_ID(s_expr));
	d_expr = pigen_expr_add_symbol(m, d_symbol, t1, name_d);
	REQUIRE(!IS_INVALID_ID(d_expr));
	dest = pigen_lvalue_resolve(m, d_expr);
	REQUIRE(!IS_INVALID_ID(dest));
	/* Seed the one-atom true guard directly in the predicate arena (the
	 * t8-module pattern): a valid guard the transfer can consume. */
	m->predicates = malloc(sizeof(*m->predicates));
	REQUIRE(m->predicates);
	m->predicates[0] = (pigen_predicate){0};
	m->predicate_count = 1;
	m->predicate_capacity = 1;
	transfer = pigen_transfer_add(m, (pigen_syntax_id){6}, u.module,
		u.process, dest, s_expr, (pigen_predicate_id){0}, u.domain, NULL, 0,
		(pigen_source_span){source, 46, 64});
	REQUIRE(!IS_INVALID_ID(transfer));
	u.transfer = transfer;
	u.transfer_span = (pigen_source_span){source, 46, 64};
	return u;
}

/* ------------------------------------------------------------------ */
/* (1) Route lifecycle: init zeroes the record and stores the module;  */
/*     free releases the node arena and zeroes the record. The build   */
/*     and check stubs report the -1 unimplemented sentinel.           */
/* ------------------------------------------------------------------ */

SECTION("route-lifecycle")
{
	pigen_source_manager sources = {0};
	pigen_semantic_model model;
	pigen_rtl_model rtl = {0};
	pigen_rtl_lowering lowering;
	struct rw_unit unit;
	pigen_route route = {0};
	int rc;

	unit = rw_unit(&model, &sources, &rtl, &lowering);
	rc = pigen_route_init(&route, unit.module);
	REQUIRE(rc == 0);
	REQUIRE(route.module == unit.module);
	REQUIRE(route.transfer_count == 0);
	REQUIRE(route.node_count == 0);
	REQUIRE(route.node_capacity == 0);
	REQUIRE(route.nodes == NULL);
	REQUIRE(route.check_valid == 0);
	REQUIRE(IS_INVALID_SPAN(route.check_span));
	/* The unimplemented build and check report -1 (the sentinel). */
	rc = pigen_route_build(&route, &lowering, unit.module);
	REQUIRE(rc == -1);
	rc = pigen_route_check(&route);
	REQUIRE(rc == -1);
	/* free releases the (empty) arena and zeroes the record. */
	rc = pigen_route_free(&route);
	REQUIRE(rc == 0);
	REQUIRE(route.module.index == PIGEN_INVALID_ID);
	REQUIRE(route.transfer_count == 0);
	REQUIRE(route.node_count == 0);
	REQUIRE(route.node_capacity == 0);
	REQUIRE(route.nodes == NULL);
	REQUIRE(route.check_valid == 0);
	REQUIRE(IS_INVALID_SPAN(route.check_span));
}

/* ------------------------------------------------------------------ */
/* (2) Route shape: build establishes the resolved route's ready      */
/*     graph - one node per direct atomic transfer, in arena order,    */
/*     each carrying the destination signal's realization-ready        */
/*     dependency; the route is isolated to its own module.            */
/* ------------------------------------------------------------------ */

SECTION("route-shape")
{
	pigen_source_manager sources = {0};
	pigen_semantic_model model;
	pigen_rtl_model rtl = {0};
	pigen_rtl_lowering lowering;
	struct rw_unit unit;
	pigen_route route = {0};
	const pigen_semantic_signal *dest;
	const pigen_semantic_transfer *tr;
	int rc;
	size_t i;

	unit = rw_unit(&model, &sources, &rtl, &lowering);
	rc = pigen_route_init(&route, unit.module);
	REQUIRE(rc == 0);
	rc = pigen_route_build(&route, &lowering, unit.module);
	REQUIRE(rc == 0);
	/* The route mirrors the resolved unit. */
	REQUIRE(route.module == unit.module);
	REQUIRE(route.transfer_count == 1);
	REQUIRE(route.node_count == 1);
	REQUIRE(route.nodes != NULL);
	REQUIRE(route.nodes[0].transfer == unit.transfer);
	/* The node carries the destination signal's realization-ready
	 * dependency (a BUF realizes an elastic slot: DOWNSTREAM). */
	dest = pigen_signal_get(&model, unit.d);
	REQUIRE(dest);
	REQUIRE(route.nodes[0].ready_dependency ==
		rw_realization_ready(dest->transfer_type));
	REQUIRE(route.nodes[0].ready_dependency ==
		PIGEN_TRANSFER_READY_DOWNSTREAM);
	/* DOWNSTREAM names the transfer the readiness depends on; for the
	 * minimal single-transfer unit that is the invalid id (no other
	 * transfer in the unit to depend on). */
	REQUIRE(IS_INVALID_ID(route.nodes[0].downstream));
	/* The module's transfer arena agrees with the route's node count and
	 * order (arena order). */
	{
		const pigen_process_id *procs;
		const pigen_transfer_id *xfers;
		size_t proc_count, xfer_count, p;
		int matches_arena = 1;

		procs = pigen_module_processes(&model, unit.module, &proc_count);
		REQUIRE(procs && proc_count == 1);
		for (p = 0; p < proc_count && matches_arena; p++)
		{
			xfers = pigen_process_transfers(&model, procs[p], &xfer_count);
			if (!xfers || xfer_count != route.node_count)
				matches_arena = 0;
			for (i = 0; i < xfer_count && matches_arena; i++)
				if (xfers[i].index != route.nodes[i].transfer.index)
					matches_arena = 0;
		}
		REQUIRE(matches_arena);
	}
	/* The transfer's original span is preserved on the semantic record
	 * (the anchor the rejected route's check_span will reuse). */
	tr = pigen_transfer_get(&model, unit.transfer);
	REQUIRE(tr && tr->span == unit.transfer_span);
	rc = pigen_route_free(&route);
	REQUIRE(rc == 0);
}

/* ------------------------------------------------------------------ */
/* (3) Route check: the resolved unit's sizing/signedness/aggregate    */
/*     laws hold and its whole-unit ready graph is cycle-free, so the  */
/*     check reports the route valid and leaves check_span untouched.  */
/* ------------------------------------------------------------------ */

SECTION("route-check")
{
	pigen_source_manager sources = {0};
	pigen_semantic_model model;
	pigen_rtl_model rtl = {0};
	pigen_rtl_lowering lowering;
	struct rw_unit unit;
	pigen_route route = {0};
	int rc;

	unit = rw_unit(&model, &sources, &rtl, &lowering);
	rc = pigen_route_init(&route, unit.module);
	REQUIRE(rc == 0);
	rc = pigen_route_build(&route, &lowering, unit.module);
	REQUIRE(rc == 0);
	/* The minimal unit is ownership-legal (the resolver's exclusive
	 * producer/consumer law admitted it), its payload sizing/signedness/
	 * aggregate agree (a 1-bit BUF to a 1-bit BUF), and its one-node ready
	 * graph has no cycle or self-loop. The check reports it valid. */
	rc = pigen_route_check(&route);
	REQUIRE(rc == 0);
	REQUIRE(route.check_valid == 1);
	/* A valid route anchors no failure: check_span stays the invalid
	 * sentinel init established. */
	REQUIRE(IS_INVALID_SPAN(route.check_span));
	rc = pigen_route_free(&route);
	REQUIRE(rc == 0);
}

int main(void)
{
	return check_finish();
}
