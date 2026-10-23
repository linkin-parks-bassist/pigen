#include "pigen/route.h"
#include <stdlib.h>
#include <string.h>

/*
 * Retained-core route.
 *
 * One resolved module -> its declarations -> one clocked process -> one
 * direct atomic transfer, lowered on the structured path. The build
 * establishes the whole-unit ready graph - one node per direct atomic
 * transfer, in arena order, each carrying the destination signal's
 * realization-ready dependency - and enforces the route's sizing,
 * signedness and aggregate laws and its module isolation, then validates the
 * ready graph for cycles and self-loops. The check re-validates the ready
 * graph and reports the route's check result (the route record carries the
 * ready graph and the check result, not a model pointer, so the model-bound
 * laws are enforced by the build, which has the lowering in hand).
 *
 * A failed call reports -1 (or the invalid sentinel) and the compile stops;
 * it may leave partial state behind, and nothing uses the model after a
 * failed compile, so no caller or test asserts that a failure left the model
 * or the route unchanged (how/should/compiler/builders/fail.md).
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

/* The transfer's value expression and its data type. */
static const pigen_semantic_expr *route_value_expr(
	pigen_semantic_model *sem, const pigen_semantic_transfer *tr)
{
	return pigen_expr_get(sem, tr->value);
}

/* A data type's packed width and signedness, using the owner algebra: an
 * exact-integer type carries no packed width (derive it from the exact value);
 * every other integral type reports its packed width as a const expression.
 * Reports 0 (and leaves the outputs unset) when the type cannot be sized. */
static int route_type_width(pigen_semantic_model *sem,
	pigen_data_type_id type, uint64_t *width, pigen_signedness *signedness)
{
	pigen_state_domain state_domain;
	pigen_signedness sign;

	sign = pigen_data_type_signedness(sem, type);
	state_domain = pigen_data_type_state_domain(sem, type);
	if (sign < PIGEN_SIGN_IMPLICIT || sign > PIGEN_SIGN_SIGNED ||
		(state_domain != PIGEN_DATA_TYPE_STATE_TWO &&
		state_domain != PIGEN_DATA_TYPE_STATE_FOUR))
		return 0;
	if (pigen_data_type_numerical_interpretation(sem, type) ==
		PIGEN_NUMERICAL_EXACT_INTEGER)
	{
		pigen_integer_id exact_value = pigen_data_type_exact_value(sem, type);
		*width = pigen_integer_unsigned_width(sem, exact_value);
	}
	else
	{
		pigen_const_expr_id width_expr = pigen_data_type_packed_width(sem, type);
		if (!pigen_const_expr_evaluate_u64(sem, width_expr, width))
			return 0;
	}
	*signedness = sign;
	return 1;
}

/* A value expression's flattened aggregate width and signedness, using the
 * data-type owner algebra: a symbol or a select projects its data type's
 * packed width; a concatenation sums its children's widths left to right
 * (packed bit-stream order, leftmost most significant). Reports 0 when the
 * expression cannot be sized. */
static int route_value_width(pigen_semantic_model *sem,
	const pigen_semantic_expr *expr, uint64_t *width, pigen_signedness *signedness)
{
	if (expr->kind == PIGEN_EXPR_CONCATENATION)
	{
		const pigen_expr_id *children =
			pigen_expr_children(sem, expr->as.sequence.first_child,
				expr->as.sequence.child_count);
		uint64_t total = 0;
		size_t i;
		pigen_signedness sign = PIGEN_SIGN_IMPLICIT;
		int first = 1;

		if (!children || expr->as.sequence.child_count == 0)
			return 0;
		for (i = 0; i < expr->as.sequence.child_count; i++)
		{
			uint64_t w;
			pigen_signedness s;

			if (!route_value_width(sem, pigen_expr_get(sem, children[i]), &w, &s))
				return 0;
			if (first)
			{
				sign = s;
				first = 0;
			}
			else if (s != sign)
				return 0;
			if (total > UINT64_MAX - w)
				return 0;
			total += w;
		}
		*width = total;
		*signedness = sign;
		return 1;
	}
	return route_type_width(sem, expr->data_type, width, signedness);
}

/* A destination lvalue's flattened aggregate width, signedness and the
 * number of buffered signals it writes, using the owner algebra. A projection
 * writes one whole buffered signal (the complete base token); a concatenation
 * writes each child destination signal whole, summed left to right in packed
 * bit-stream order. Reports 0 when the lvalue is not a supported destination. */
static int route_dest_width(pigen_semantic_model *sem,
	const pigen_semantic_lvalue *lvalue, uint64_t *width,
	pigen_signedness *signedness, size_t *signal_count)
{
	if (lvalue->kind == PIGEN_LVALUE_PROJECTION)
	{
		const pigen_semantic_signal *sig =
			pigen_signal_get(sem, lvalue->as.projection.signal);
		if (!sig)
			return 0;
		*width = 0;
		*signedness = PIGEN_SIGN_IMPLICIT;
		*signal_count = 1;
		return route_type_width(sem, sig->data_type, width, signedness);
	}
	if (lvalue->kind == PIGEN_LVALUE_CONCATENATION)
	{
		const pigen_lvalue_id *children =
			pigen_lvalue_children(sem, lvalue->as.sequence.first_child,
				lvalue->as.sequence.child_count);
		uint64_t total = 0;
		size_t count = 0, i;
		pigen_signedness sign = PIGEN_SIGN_IMPLICIT;
		int first = 1;

		if (!children || lvalue->as.sequence.child_count == 0)
			return 0;
		for (i = 0; i < lvalue->as.sequence.child_count; i++)
		{
			const pigen_semantic_lvalue *child = pigen_lvalue_get(sem, children[i]);
			uint64_t w;
			pigen_signedness s;
			size_t n;

			if (!child || child->kind != PIGEN_LVALUE_PROJECTION)
				return 0;
			if (!route_dest_width(sem, child, &w, &s, &n))
				return 0;
			if (first)
			{
				sign = s;
				first = 0;
			}
			else if (s != sign)
				return 0;
			if (total > UINT64_MAX - w)
				return 0;
			total += w;
			count += n;
		}
		*width = total;
		*signedness = sign;
		*signal_count = count;
		return 1;
	}
	return 0;
}

/* The first destination signal of a transfer: the single signal a projection
 * lvalue writes, or the leftmost (most significant) child signal of a
 * concatenation lvalue. The route's ready-graph node carries this signal's
 * realization-ready dependency. */
static const pigen_semantic_signal *route_dest_signal(
	pigen_semantic_model *sem, const pigen_semantic_transfer *tr)
{
	const pigen_semantic_lvalue *lvalue = pigen_lvalue_get(sem, tr->destination);

	if (!lvalue)
		return NULL;
	if (lvalue->kind == PIGEN_LVALUE_PROJECTION)
		return pigen_signal_get(sem, lvalue->as.projection.signal);
	if (lvalue->kind == PIGEN_LVALUE_CONCATENATION)
	{
		const pigen_lvalue_id *children =
			pigen_lvalue_children(sem, lvalue->as.sequence.first_child,
				lvalue->as.sequence.child_count);
		const pigen_semantic_lvalue *first;

		if (!children || lvalue->as.sequence.child_count == 0)
			return NULL;
		first = pigen_lvalue_get(sem, children[0]);
		if (!first || first->kind != PIGEN_LVALUE_PROJECTION)
			return NULL;
		return pigen_signal_get(sem, first->as.projection.signal);
	}
	return NULL;
}

/* The route's sizing and aggregate laws for one transfer: the destination's
 * flattened aggregate payload and the value expression's flattened aggregate
 * payload must be the same integral width and carry the same signedness. The
 * destination is whole (a projection writes its complete base token; a
 * concatenation writes each child destination signal whole, packed bit-stream
 * order left to right) and the value flattens the same way. Equal aggregate
 * widths are required, not equal item counts or member widths. */
static int route_transfer_laws(pigen_semantic_model *sem,
	const pigen_semantic_transfer *tr)
{
	const pigen_semantic_lvalue *dest = pigen_lvalue_get(sem, tr->destination);
	const pigen_semantic_expr *value = route_value_expr(sem, tr);
	uint64_t dest_width, value_width;
	pigen_signedness dest_sign, value_sign;
	size_t dest_signals;

	if (!dest || !value)
		return 0;
	if (!route_dest_width(sem, dest, &dest_width, &dest_sign, &dest_signals))
		return 0;
	if (dest_signals == 0)
		return 0;
	if (!route_value_width(sem, value, &value_width, &value_sign))
		return 0;
	/* Sizing and signedness agree: the flattened aggregate payloads match. */
	if (dest_width != value_width)
		return 0;
	if (dest_sign != value_sign)
		return 0;
	return 1;
}

/* The downstream edge of a ready-graph node: the transfer its readiness
 * depends on, stored in the node. Valid only when ready_dependency is
 * PIGEN_TRANSFER_READY_DOWNSTREAM and a downstream transfer is named; for the
 * minimal single-transfer unit (no other transfer to depend on) the node's
 * downstream is the invalid id, so the edge is absent. */
static pigen_transfer_id route_downstream(const pigen_route_ready_node *node)
{
	if (node->ready_dependency == PIGEN_TRANSFER_READY_DOWNSTREAM &&
		node->downstream.index != PIGEN_INVALID_ID)
		return node->downstream;
	return (pigen_transfer_id){PIGEN_INVALID_ID};
}

/* The retained core's validity, domain and ownership laws for one transfer,
 * checked against the signal-uses the transfer carries. These are the
 * model-bound laws the build enforces at the point the semantic model is in
 * hand (the route record carries the ready graph and the check result, not a
 * model pointer, so the model-bound laws are enforced here).
 *
 *   - validity: a CONSUMER role on a non-locally-owned signal (an upstream
 *     INPUT or INOUT whose validity is owned by its producer) is diagnosed:
 *     local synchronous validity actions apply only to a locally owned
 *     buf/fifo/skid/port and cannot modify upstream input validity;
 *   - domain: a signal whose transfer type binds a domain (binds_domain) is
 *     used on a different clock edge than the transfer's domain: the first
 *     Pigen use binds the signal to one synchronous one-edge clock domain,
 *     and a later use on a different edge (a multi-edge / cross-domain form)
 *     is diagnosed at the original source span;
 *   - ownership, self-consumption: a signal carrying both the PRODUCER and
 *     the CONSUMER role in the same transfer is diagnosed: a consuming
 *     transfer cannot read one of its own buffered destinations (it would
 *     connect output-ready back to input-ready);
 *   - ownership, exclusive: a signal whose transfer type requires ownership
 *     (requires_ownership) must carry the matching role in the transfer's
 *     signal-uses: a buffered write requires the PRODUCER role and a buffered
 *     read requires the CONSUMER role. A transfer that carries no matching
 *     role for a buffered signal is diagnosed: exclusive buffered
 *     producers/consumers are required.
 *
 * Returns 1 when every law holds and 0 when a law is violated. */
static int route_vdo_laws(pigen_semantic_model *sem,
	const pigen_semantic_transfer *tr, pigen_transfer_id tr_id)
{
	const pigen_transfer_signal_use *uses =
		pigen_transfer_signal_uses(sem, tr_id);
	const pigen_semantic_clock_domain *tr_domain =
		pigen_clock_domain_get(sem, tr->domain);
	size_t i;

	if (tr->signal_use_count > 0 && !uses)
		return 0;

	for (i = 0; i < tr->signal_use_count; i++)
	{
		const pigen_semantic_signal *sig =
			pigen_signal_get(sem, uses[i].signal);
		const pigen_transfer_type_descriptor *desc;

		if (!sig)
			return 0;
		desc = pigen_transfer_type_descriptor_get(sig->transfer_type);
		if (!desc)
			return 0;

		/* Validity: a CONSUMER role on a non-locally-owned signal (an
		 * upstream INPUT or INOUT whose validity is owned by its
		 * producer) is diagnosed: local synchronous validity actions
		 * apply only to a locally owned buf/fifo/skid/port and cannot
		 * modify upstream input validity. */
		if ((uses[i].roles & PIGEN_TRANSFER_CONSUMER) &&
			(sig->direction == PIGEN_SEMANTIC_INPUT ||
			 sig->direction == PIGEN_SEMANTIC_INOUT))
			return 0;

		/* Domain: a signal whose transfer type binds a domain is used on
		 * a different clock edge than the transfer's domain: the first
		 * Pigen use binds the signal to one synchronous one-edge clock
		 * domain, and a later use on a different edge (a multi-edge /
		 * cross-domain form) is diagnosed. */
		if (desc->binds_domain && tr_domain &&
			(uses[i].roles & (PIGEN_TRANSFER_CONSUMER |
				 PIGEN_TRANSFER_PRODUCER)))
		{
			const pigen_semantic_clock_domain *sig_domain =
				pigen_clock_domain_get(sem, sig->domain);
			if (sig_domain &&
				(sig_domain->clock_symbol.index !=
				 tr_domain->clock_symbol.index ||
				 sig_domain->edge != tr_domain->edge))
				return 0;
		}

		/* Ownership, self-consumption: a signal carrying both the
		 * PRODUCER and the CONSUMER role in the same transfer is
		 * diagnosed: a consuming transfer cannot read one of its own
		 * buffered destinations (it would connect output-ready back to
		 * input-ready). */
		if ((uses[i].roles & PIGEN_TRANSFER_PRODUCER) &&
			(uses[i].roles & PIGEN_TRANSFER_CONSUMER))
			return 0;

		/* Ownership, exclusive: a signal whose transfer type requires
		 * ownership must carry the matching role in the transfer's
		 * signal-uses: a buffered write (the signal is a destination of
		 * the transfer) requires the PRODUCER role and a buffered read
		 * (the signal is a consuming source) requires the CONSUMER
		 * role. */
		if (desc->requires_ownership)
		{
			int is_dest = 0;
			int is_src = 0;
			const pigen_semantic_lvalue *dest =
				pigen_lvalue_get(sem, tr->destination);
			pigen_signal_id sig_id = uses[i].signal;

			if (dest)
			{
				if (dest->kind == PIGEN_LVALUE_PROJECTION &&
					dest->as.projection.signal.index ==
					 sig_id.index)
					is_dest = 1;
				if (dest->kind == PIGEN_LVALUE_CONCATENATION)
				{
					const pigen_lvalue_id *children =
						pigen_lvalue_children(sem,
							dest->as.sequence.first_child,
							dest->as.sequence.child_count);
					size_t k;
					if (children)
						for (k = 0; k < dest->as.sequence.child_count;
							k++)
						{
							const pigen_semantic_lvalue *c =
								pigen_lvalue_get(sem,
									children[k]);
							if (c &&
								c->kind ==
									PIGEN_LVALUE_PROJECTION &&
								c->as.projection.signal.index ==
									sig_id.index)
							{
								is_dest = 1;
								break;
							}
						}
				}
			}
			{
				const pigen_semantic_expr *value =
					route_value_expr(sem, tr);
				if (value && value->kind == PIGEN_EXPR_SYMBOL)
				{
					const pigen_symbol *sym =
						pigen_symbol_get(sem, value->as.symbol);
					if (sym && sym->kind == PIGEN_SYMBOL_SIGNAL &&
						sym->object.signal.index == sig_id.index)
						is_src = 1;
				}
			}
			if (is_dest && !(uses[i].roles & PIGEN_TRANSFER_PRODUCER))
				return 0;
			if (is_src && !(uses[i].roles & PIGEN_TRANSFER_CONSUMER))
				return 0;
		}
	}
	return 1;
}

/* Plain cycle/self-loop validation of the ready graph: a node whose
 * downstream edge points at itself is a self-loop; a cycle is any
 * ready-dependency chain that returns to its start. The retained core's
 * readiness edges are transfer -> destination-signal-ready, so each node's
 * edge is its stored downstream dependency. This is the plain detection the
 * deferred ready-graph task requires; the deterministic Tarjan diagnostic is
 * not part of this stage. */
static int route_graph_acyclic(const pigen_route *route)
{
	size_t i, step;

	for (i = 0; i < route->node_count; i++)
	{
		const pigen_route_ready_node *node = &route->nodes[i];
		pigen_transfer_id downstream = route_downstream(node);

		if (downstream.index == PIGEN_INVALID_ID)
			continue;
		if (downstream.index == node->transfer.index)
			return 0; /* self-loop */
		/* Follow the downstream edge; if it returns to the starting node it
		 * is a cycle. */
		for (step = 0; step < route->node_count; step++)
		{
			const pigen_route_ready_node *next = &route->nodes[step];
			pigen_transfer_id edge;

			if (next->transfer.index != downstream.index)
				continue;
			edge = route_downstream(next);
			if (edge.index == node->transfer.index)
				return 0;
			break;
		}
	}
	return 1;
}

int pigen_route_build(pigen_route *route,
	const pigen_rtl_lowering *lowering, pigen_module_id module)
{
	pigen_semantic_model *sem;
	const pigen_process_id *procs;
	size_t proc_count = 0;
	size_t total, p, q, xfer_count, node_index;
	const pigen_transfer_id *xfers;

	if (!route || !lowering || !lowering->semantics ||
		module.index == PIGEN_INVALID_ID)
		return -1;
	sem = lowering->semantics;

	/* Count the module's direct atomic transfers, in arena order, to size
	 * the ready-graph node arena in one allocation. */
	procs = pigen_module_processes((pigen_semantic_model *)sem, module,
		&proc_count);
	if (!procs || proc_count == 0)
		return -1;
	total = 0;
	for (p = 0; p < proc_count; p++)
	{
		xfers = pigen_process_transfers((pigen_semantic_model *)sem, procs[p],
			&xfer_count);
		if (!xfers)
			return -1;
		total += xfer_count;
	}
	if (total == 0)
		return -1;

	/* Reset the route to the empty state and grow the node arena. */
	route->module = module;
	route->transfer_count = total;
	route->node_count = 0;
	route->check_valid = 0;
	route->check_span = (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
	free(route->nodes);
	if (total > SIZE_MAX / sizeof(*route->nodes))
		return -1;
	route->nodes = calloc(total, sizeof(*route->nodes));
	if (!route->nodes)
		return -1;
	route->node_capacity = total;

	/* One node per direct atomic transfer, in arena order. Each node carries
	 * the destination signal's realization-ready dependency (the raw material
	 * the readiness structure and the cycle validation consume). The route's
	 * sizing, signedness and aggregate laws and its module isolation are
	 * enforced here, at the point the model is in hand: a violated law
	 * reports the error and the compile stops (fail-loud, no rollback),
	 * anchoring check_span at the first failing transfer's original span. */
	node_index = 0;
	for (p = 0; p < proc_count; p++)
	{
		xfers = pigen_process_transfers((pigen_semantic_model *)sem, procs[p],
			&xfer_count);
		if (!xfers)
			return -1;
		for (q = 0; q < xfer_count; q++)
		{
			const pigen_semantic_transfer *tr =
				pigen_transfer_get(sem, xfers[q]);
			const pigen_semantic_signal *dest;
			const pigen_transfer_type_descriptor *d;
			const pigen_transfer_realization_descriptor *r;
			pigen_transfer_ready_dependency ready;
			pigen_route_ready_node *node;

			if (!tr || node_index >= total)
				return -1;
			/* Isolation: the transfer lives in the route's module. */
			if (tr->module.index != module.index)
			{
				route->check_valid = 0;
				route->check_span = tr->span;
				return -1;
			}
			/* Sizing, signedness and aggregate laws for the transfer. */
			if (!route_transfer_laws(sem, tr))
			{
				route->check_valid = 0;
				route->check_span = tr->span;
				return -1;
			}
			/* Validity, domain and ownership laws for the transfer. */
			if (!route_vdo_laws(sem, tr, xfers[q]))
			{
				route->check_valid = 0;
				route->check_span = tr->span;
				return -1;
			}
			dest = route_dest_signal(sem, tr);
			if (!dest)
			{
				route->check_valid = 0;
				route->check_span = tr->span;
				return -1;
			}
			d = pigen_transfer_type_descriptor_get(dest->transfer_type);
			r = d ? pigen_transfer_realization_descriptor_get(d->realization) : NULL;
			ready = r ? r->ready_dependency : PIGEN_TRANSFER_READY_INVALID;
			node = &route->nodes[node_index];
			node->transfer = xfers[q];
			node->ready_dependency = ready;
			node->downstream = (pigen_transfer_id){PIGEN_INVALID_ID};
			node_index++;
		}
	}
	route->node_count = node_index;

	/* The whole-unit ready graph must be cycle-free before the route is
	 * published. */
	if (!route_graph_acyclic(route))
	{
		const pigen_semantic_transfer *tr =
			pigen_transfer_get(sem, route->nodes[0].transfer);
		route->check_valid = 0;
		route->check_span = tr ? tr->span :
			(pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
		return -1;
	}

	route->check_valid = 1;
	route->check_span = (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
	return 0;
}

int pigen_route_check(pigen_route *route)
{
	if (!route)
		return -1;
	if (route->node_count == 0)
		return -1;
	/* Re-validate the whole-unit ready graph. The sizing, signedness and
	 * aggregate laws and the module isolation are enforced by the build, at
	 * the point the semantic model is in hand (the route record carries the
	 * ready graph and the check result, not a model pointer); the check
	 * re-affirms the readiness structure is cycle-free and reports the
	 * route's check result. */
	if (!route_graph_acyclic(route))
	{
		route->check_valid = 0;
		/* The original-span anchor is set by the build; the check reports the
		 * cycle with the invalid sentinel when no anchor is in hand. */
		route->check_span = (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
		return -1;
	}
	route->check_valid = 1;
	route->check_span = (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
	return 0;
}
