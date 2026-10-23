#ifndef PIGEN_ROUTE_H
#define PIGEN_ROUTE_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/transfer_type.h"

/* The retained-core route: one resolved module -> its declarations -> one
 * clocked process -> one direct atomic transfer, lowered on the structured
 * path. It reuses the landed shared signal arena/binding, the data-type owner
 * algebra, the transfer types and declaration lowering (the elastic RTL
 * lowering entry points pigen_lower_rtl_module and pigen_lower_rtl_transfers)
 * and adds the semantic sizing, signedness and aggregate checks the spec
 * requires of the route, exposing the whole-unit ready graph as the route's
 * readiness structure.
 *
 * The build establishes the ready graph (one node per direct atomic
 * transfer, in arena order, each carrying the destination signal's
 * realization-ready dependency), enforces the route's module isolation and
 * sizing/signedness/aggregate laws, validates the ready graph
 * cycle/self-loop-free and anchors check_span at the first failing
 * transfer's original span on rejection. The check re-validates the ready
 * graph and reports the route's check result; the model-bound laws are
 * enforced by the build, which holds the lowering.
 *
 * A failed call reports -1 (or the invalid sentinel) and the compile stops;
 * it may leave partial state behind, and nothing uses the model after a
 * failed compile, so no caller or test asserts that a failure left the
 * model or the route unchanged (how/should/compiler/builders/fail.md). */

/* One node of the whole-unit ready graph, one per direct atomic transfer in
 * the unit. ready_dependency is the transfer's readiness dependency taken
 * from its realization (transfer_type.h); it is the raw material the
 * ready-graph construction and the cycle/self-loop validation consume.
 * downstream names the transfer whose readiness this one depends on: valid
 * only when ready_dependency is PIGEN_TRANSFER_READY_DOWNSTREAM, else the
 * invalid id. transfer is the owning transfer. */
typedef struct {
	pigen_transfer_id transfer;
	pigen_transfer_ready_dependency ready_dependency;
	pigen_transfer_id downstream;
} pigen_route_ready_node;

/* The route's readiness structure for one unit: the whole-unit ready graph
 * (one node per direct atomic transfer, in arena order) plus the route's
 * sizing/signedness/aggregate check result. transfer_count and node_count
 * name the graph's size; a zero count is the empty state. check_valid is the
 * sizing/signedness/aggregate check result for the route and check_span the
 * original span anchoring the first failed check (a rejected route anchors
 * its diagnostic here); the exact per-law dispatch is settled by the
 * implementation stage. */
typedef struct {
	pigen_module_id module;
	size_t transfer_count;
	size_t node_count;
	pigen_route_ready_node *nodes;
	size_t node_capacity;
	int check_valid;
	pigen_source_span check_span;
} pigen_route;

/* Zeroes the record and leaves the ready graph empty (no nodes allocated,
 * no capacity). The module pointer is stored as given; the graph grows on
 * demand when the route build publishes a node. */
void pigen_route_init(pigen_route *route, pigen_module_id module);

/* Releases the ready-graph node arena and zeroes the record. The owned
 * semantic model and RTL lowering are not released by this call; the caller
 * keeps them. */
void pigen_route_free(pigen_route *route);

/* Establishes the resolved route one module -> declarations -> clocked
 * process -> direct atomic transfer on the structured path, reusing the
 * landed lowering entry points, and fills the route's ready graph with one
 * node per direct atomic transfer plus the sizing/signedness/aggregate
 * check result. lowering is the already-initialized lowering over the
 * resolved module's semantic model and RTL model; module is the resolved
 * route module. Returns 0 on success and -1 on error; a failed call reports
 * the error and the compile stops - it may leave partial state behind, and
 * no test asserts that a failure left the model or the route unchanged. */
int pigen_route_build(pigen_route *route,
	const pigen_rtl_lowering *lowering, pigen_module_id module);

/* Checks the route's sizing, signedness and aggregate laws and its
 * whole-unit ready graph (rejecting strongly connected cycles and
 * self-loops that cross no deliberate ready break). On failure it sets the
 * route's check result to invalid and anchors check_span at the first
 * failing transfer's original span. Returns 0 when the route is valid and
 * -1 when a law is violated; a failed call reports the error and the
 * compile stops. The sizing, signedness and aggregate laws and the module
 * isolation are enforced by the build, at the point the semantic model is
 * in hand; the check re-affirms the readiness structure is cycle-free. */
int pigen_route_check(pigen_route *route);

#endif
