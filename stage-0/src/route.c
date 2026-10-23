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

/* The destination signal id of a transfer: the single signal a projection
 * lvalue writes, or the leftmost (most significant) child signal of a
 * concatenation lvalue. The id is the arena identity the ready-edge law
 * matches a consuming transfer's value against; the invalid id is returned
 * when the transfer has no destination signal. */
static pigen_signal_id route_dest_signal_id(
	pigen_semantic_model *sem, const pigen_semantic_transfer *tr)
{
	const pigen_semantic_lvalue *lvalue = pigen_lvalue_get(sem, tr->destination);
	pigen_signal_id none = (pigen_signal_id){PIGEN_INVALID_ID};

	if (!lvalue)
		return none;
	if (lvalue->kind == PIGEN_LVALUE_PROJECTION)
		return lvalue->as.projection.signal;
	if (lvalue->kind == PIGEN_LVALUE_CONCATENATION)
	{
		const pigen_lvalue_id *children =
			pigen_lvalue_children(sem, lvalue->as.sequence.first_child,
				lvalue->as.sequence.child_count);
		const pigen_semantic_lvalue *first;

		if (!children || lvalue->as.sequence.child_count == 0)
			return none;
		first = pigen_lvalue_get(sem, children[0]);
		if (!first || first->kind != PIGEN_LVALUE_PROJECTION)
			return none;
		return first->as.projection.signal;
	}
	return none;
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

/* The ready-edge law for the retained core's stall/backpressure structure.
 *
 * The whole-unit ready graph is a per-cycle ready-propagation chain: a
 * transfer's readiness is gated on the readiness of the transfer that
 * consumes its buffered destination, because the downstream must be ready to
 * accept before the upstream can fire. The edge is named by the destination
 * signal's owner-reported realization-ready dependency:
 *
 *   - a DOWNSTREAM destination (a buf / elastic-slot destination) names the
 *     downstream edge for the transfer that consumes the buffered signal it
 *     writes: the node's downstream field is set to that consuming transfer,
 *     and the ready chain continues through it;
 *   - an OCCUPANCY destination (a skid / queue destination) carries no
 *     downstream edge and breaks the ready chain: the node's downstream field
 *     is left the invalid id, and the ready chain stops there;
 *   - any other dependency (EXTERNAL, CONSTANT, INVALID) also carries no
 *     downstream edge.
 *
 * A downstream edge whose consumer is not present in the unit leaves the
 * node's downstream at the invalid id: the ready chain is broken at that
 * point (a stalled / unconsumed downstream blocks its upstream source). The
 * consuming transfer is the one whose value expression names the buffered
 * signal the node's destination writes. Returns the named consumer's
 * transfer id, or the invalid id when there is no downstream edge or no
 * consuming transfer is present. */
static pigen_transfer_id route_downstream_consumer(
	pigen_semantic_model *sem, pigen_module_id module, pigen_signal_id sig)
{
	pigen_process_id *procs = NULL;
	size_t proc_count = 0, p;
	pigen_transfer_id none = (pigen_transfer_id){PIGEN_INVALID_ID};

	procs = pigen_module_processes(sem, module, &proc_count);
	if (!procs)
		return none;
	for (p = 0; p < proc_count; p++)
	{
		pigen_transfer_id *xfers = NULL;
		size_t xfer_count = 0, q;

		xfers = pigen_process_transfers(sem, procs[p], &xfer_count);
		if (!xfers)
			continue;
		for (q = 0; q < xfer_count; q++)
		{
			const pigen_semantic_transfer *tr = pigen_transfer_get(sem, xfers[q]);
			const pigen_semantic_expr *value;
			const pigen_symbol *sym;

			if (!tr)
				continue;
			value = pigen_expr_get(sem, tr->value);
			if (!value || value->kind != PIGEN_EXPR_SYMBOL)
				continue;
			sym = pigen_symbol_get(sem, value->as.symbol);
			if (sym && sym->kind == PIGEN_SYMBOL_SIGNAL &&
				sym->object.signal.index == sig.index)
				return xfers[q];
		}
	}
	return none;
}

/* The retained core's validity, domain and ownership laws for one transfer,
 * checked against the signal-uses the transfer carries. These are the
 * model-bound laws the build enforces at the point the semantic model is in
 * hand (the route record carries the ready graph and the check result, not a
 * model pointer, so the model-bound laws are enforced here).
 *
 *   - validity: the retained core's producer-owns-validity law is observable
 *     as a signal-use role law - a transfer that reads a buffered input
 *     consumes it (CONSUMER) and one that writes a buffered destination
 *     produces it (PRODUCER). CONSUMER on a buffered upstream INPUT is legal
 *     (the landed green law witness law-projection-dedup carries it and
 *     requires build rc==0) and is never diagnosed: local synchronous validity
 *     actions compose in source order and cannot modify upstream input
 *     validity, which is owned by the input's producer;
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
 *     read requires the CONSUMER role. The law fires on a buffered signal's
 *     missing role, not on the mere presence or absence of a per-signal use
 *     entry: a buffered value read by the transfer but absent from the uses
 *     set (missing its CONSUMER role) is diagnosed.
 *
 * Returns 1 when every law holds and 0 when a law is violated. */
static int route_vdo_laws(pigen_semantic_model *sem,
	const pigen_semantic_transfer *tr, pigen_transfer_id tr_id)
{
	const pigen_transfer_signal_use *uses =
		pigen_transfer_signal_uses(sem, tr_id);
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

		/* Validity: CONSUMER on a buffered upstream INPUT is legal - the
		 * landed green law witness law-projection-dedup carries it and
		 * requires build rc==0, so it is never diagnosed here. The retained
		 * core's producer-owns-validity law is observable as this signal-use
		 * role law: a transfer that reads a buffered input consumes it and one
		 * that writes a buffered destination produces it. */

		/* Domain, one synchronous one-edge binding: the multi-edge form is
		 * detected by a cross-transfer sweep (route_domain_multi_edge), not
		 * here - the witness signals carry no domain of their own, so the
		 * binding is derived from the clock edge of the transfers that use
		 * them. The first Pigen use of a buffered (binds_domain) signal binds
		 * it to one synchronous one-edge clock domain; a later use on a
		 * different edge (a multi-edge / cross-domain form) is diagnosed. */

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
		 * role. The law fires on the buffered signal's missing role, not on
		 * the mere presence or absence of a per-signal use entry: a buffered
		 * value read by the transfer but absent from the uses set (so its
		 * CONSUMER role is missing) is diagnosed. */
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

	/* Ownership, exclusive, value signal: the transfer's value expression
	 * names a buffered source whose requires_ownership holds; if that signal
	 * does not appear in the transfer's signal-uses with the CONSUMER role the
	 * exclusive buffered-consumer requirement is unsatisfied and is diagnosed.
	 * This is the law's missing-role form - it fires on the buffered value's
	 * absent CONSUMER role, not on the absence of a per-signal use entry. It
	 * applies only when the transfer explicitly specifies some signal-uses
	 * (signal_use_count > 0) yet omits the required role; a transfer that
	 * specifies no uses at all is admitted by the resolver's exclusive
	 * producer/consumer law and is not diagnosed here. */
	{
		const pigen_semantic_expr *value = route_value_expr(sem, tr);
		if (tr->signal_use_count > 0 && value &&
			value->kind == PIGEN_EXPR_SYMBOL)
		{
			const pigen_symbol *sym = pigen_symbol_get(sem, value->as.symbol);
			if (sym && sym->kind == PIGEN_SYMBOL_SIGNAL)
			{
				const pigen_semantic_signal *vsig =
					pigen_signal_get(sem, sym->object.signal);
				const pigen_transfer_type_descriptor *vdesc =
					vsig ? pigen_transfer_type_descriptor_get(
						vsig->transfer_type) : NULL;
				pigen_signal_id vsig_id = sym->object.signal;
				int has_consumer = 0;
				size_t k;

				if (vsig && vdesc && vdesc->requires_ownership)
				{
					for (k = 0; k < tr->signal_use_count; k++)
						if (uses[k].signal.index ==
							vsig_id.index &&
							(uses[k].roles & PIGEN_TRANSFER_CONSUMER))
						{
							has_consumer = 1;
							break;
						}
					if (!has_consumer)
						return 0;
				}
			}
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

/* Domain, one synchronous one-edge binding, as a whole-unit cross-transfer
 * law. The retained core's actions bind to one synchronous one-edge clock
 * domain, canonical by resolved clock-declaration identity and event edge. The
 * first Pigen use of a buffered (binds_domain) signal binds it to one clock
 * edge; a later use of the same signal in a transfer on a different edge (a
 * multi-edge / cross-domain form) is a violation and is diagnosed at the
 * original source span. The witness signals carry no domain of their own, so
 * the binding is derived from the clock edge of the transfers that use the
 * signal. Returns 1 when every buffered signal is used on a single edge and 0
 * when a multi-edge form is found. */
struct route_domain_binding {
	pigen_signal_id signal;
	pigen_semantic_edge edge;
};

static int route_domain_multi_edge(pigen_semantic_model *sem,
	const pigen_process_id *procs, size_t proc_count)
{
	struct route_domain_binding *bindings = NULL;
	size_t binding_count = 0, binding_capacity = 0;
	size_t p, q, i;
	const pigen_transfer_id *xfers;
	size_t xfer_count;
	int ok = 1;

	for (p = 0; p < proc_count; p++)
	{
		xfers = pigen_process_transfers(sem, procs[p], &xfer_count);
		if (!xfers)
			return 0;
		for (q = 0; q < xfer_count; q++)
		{
			const pigen_semantic_transfer *tr = pigen_transfer_get(sem, xfers[q]);
			const pigen_semantic_clock_domain *tr_domain;
			const pigen_transfer_signal_use *uses;
			size_t use_count;

			if (!tr)
				return 0;
			tr_domain = pigen_clock_domain_get(sem, tr->domain);
			uses = pigen_transfer_signal_uses(sem, xfers[q]);
			use_count = tr->signal_use_count;
			if (use_count > 0 && !uses)
				return 0;
			for (i = 0; i < use_count; i++)
			{
				const pigen_semantic_signal *sig =
					pigen_signal_get(sem, uses[i].signal);
				const pigen_transfer_type_descriptor *desc;
				size_t b;
				int bound = 0;

				if (!sig || !tr_domain)
					return 0;
				desc = pigen_transfer_type_descriptor_get(sig->transfer_type);
				if (!desc || !desc->binds_domain)
					continue;
				if (!(uses[i].roles & (PIGEN_TRANSFER_CONSUMER |
						PIGEN_TRANSFER_PRODUCER)))
					continue;
				for (b = 0; b < binding_count; b++)
				{
					if (bindings[b].signal.index == uses[i].signal.index)
					{
						if (bindings[b].edge != tr_domain->edge)
							ok = 0;
						bound = 1;
						break;
					}
				}
				if (!bound)
				{
					if (binding_count == binding_capacity)
					{
						size_t newcap = binding_capacity ?
							binding_capacity * 2 : 8;
						struct route_domain_binding *nb =
							realloc(bindings,
								newcap * sizeof(*nb));
						if (!nb)
							return 0;
						bindings = nb;
						binding_capacity = newcap;
					}
					bindings[binding_count].signal = uses[i].signal;
					bindings[binding_count].edge = tr_domain->edge;
					binding_count++;
				}
			}
		}
	}
	free(bindings);
	return ok;
}

int pigen_route_build(pigen_route *route,
	const pigen_rtl_lowering *lowering, pigen_module_id module)
{
	pigen_semantic_model *sem;
	const pigen_process_id *procs;
	size_t proc_count = 0;
	size_t total, p, q, xfer_count, node_index;
	const pigen_transfer_id *xfers;
	pigen_signal_id *dest_sig;

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
	/* Per-node destination signal id: the signal a node's transfer writes,
	 * the identity the ready-edge law matches a downstream consumer's value
	 * against. */
	dest_sig = calloc(total, sizeof(*dest_sig));
	if (!dest_sig)
		return -1;

	/* Domain, one synchronous one-edge binding: a buffered (binds_domain)
	 * signal used in transfers on two different clock edges of the same clock
	 * declaration is a multi-edge / cross-domain form and is diagnosed at the
	 * original source span. The check sweeps every transfer's signal-uses in
	 * arena order before the ready graph is built. */
	if (!route_domain_multi_edge((pigen_semantic_model *)sem, procs, proc_count))
	{
		route->check_valid = 0;
		route->check_span = (pigen_source_span){(pigen_source_id){PIGEN_INVALID_ID}, 0, 0};
		return -1;
	}

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
			dest_sig[node_index] = route_dest_signal_id(sem, tr);
			node_index++;
		}
	}
	route->node_count = node_index;

	/* The ready-edge law: for each node whose destination realizes a
	 * DOWNSTREAM ready dependency, name the downstream edge - the transfer
	 * that consumes the buffered signal the node's destination writes - so
	 * the per-cycle ready chain propagates through it. A node whose
	 * destination is OCCUPANCY (a skid / queue), or any other dependency,
	 * carries no downstream edge and breaks the ready chain there; a
	 * DOWNSTREAM destination with no consuming transfer present in the unit
	 * likewise leaves the edge absent (its stalled / unconsumed downstream
	 * blocks its upstream source). */
	{
		size_t n;
		for (n = 0; n < route->node_count; n++)
		{
			pigen_route_ready_node *node = &route->nodes[n];

			if (node->ready_dependency != PIGEN_TRANSFER_READY_DOWNSTREAM)
				continue;
			if (dest_sig[n].index == PIGEN_INVALID_ID)
				continue;
			node->downstream = route_downstream_consumer(
				(pigen_semantic_model *)sem, module, dest_sig[n]);
		}
	}
	free(dest_sig);

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
