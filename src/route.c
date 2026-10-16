#include "pigen/route.h"
#include <stdlib.h>

/*
 * Retained-core route skeleton.
 *
 * This establishes the shape and the build seam for the retained-core
 * structured route: one resolved module -> declarations -> clocked process
 * -> direct atomic transfer, with the whole-unit ready graph as the route's
 * readiness structure and the semantic sizing/signedness/aggregate checks.
 *
 * Only the lifecycle (init/free) is implemented; the route build and the
 * sizing/signedness/aggregate check are stubs that report the unimplemented
 * sentinel without touching the ready graph or the check result. The exact
 * behavior - the ready-graph construction, the sizing/signedness/aggregate
 * law dispatch, the cycle/self-loop validation and the original-span
 * diagnostic - arrives with the test-contract and implementation stages.
 */

void pigen_route_init(pigen_route *route, pigen_module_id module)
{
	if (!route)
		return;
	route->module = module;
	route->transfer_count = 0;
	route->node_count = 0;
	route->nodes = NULL;
	route->node_capacity = 0;
	route->check_valid = 0;
	route->check_span = (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
}

void pigen_route_free(pigen_route *route)
{
	if (!route)
		return;
	free(route->nodes);
	route->nodes = NULL;
	route->module = (pigen_module_id){PIGEN_INVALID_ID};
	route->transfer_count = 0;
	route->node_count = 0;
	route->node_capacity = 0;
	route->check_valid = 0;
	route->check_span = (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
}

int pigen_route_build(pigen_route *route,
	const pigen_rtl_lowering *lowering, pigen_module_id module)
{
	(void)route;
	(void)lowering;
	(void)module;
	return -1;
}

int pigen_route_check(pigen_route *route)
{
	(void)route;
	return -1;
}
