#include <stddef.h>
#include <string.h>

#include "pigen/fsm_lower.h"

#include "pigen/data_type.h"
#include "pigen/preprocess.h"

/* The FSM parent-module lowering owner (step (s5) of the FSM frontier, the
 * (s5) boundary of the FSM parent-module lowering). This file lowers one
 * PIGEN_SYNTAX_FSM statement (a module-level fsm item) through the common RTL
 * model, publishing into the parent module's RTL model, as one synchronous
 * controller. The controller carries the FSM's three behavioral obligations
 * structurally, the way the step-(c3) pipeline lowering carries its guarded
 * ready/valid handshake (a structurally-present gate expression per gate, the
 * gate's exact expression deferred to the full lowering):
 *
 *   - obligation a, the state register reset to the header initial state: one
 *     synchronous register holding the FSM's state, driven to the header
 *     initial state on reset (what/is/an/fsm.md);
 *   - obligation b, the per-state action bodies: each state's ordinary
 *     statements, lowered through the shared signal/expression/guard/domain/
 *     ownership services (not private resolvers) and gated so each state's
 *     actions run only while that state is active (how/to/write/fsm/states.md:
 *     "actions run only when state active");
 *   - obligation c, the guard-gated goto transitions: each goto changes the
 *     state only under its explicit enclosing guard and never implicitly waits
 *     for a transfer (does/goto/wait/for/a/transfer.md); the next-state value
 *     is conditioned on the goto's guard so the state holds otherwise, and the
 *     transitions share the one state register as their single destination
 *     (structural exclusion of overlapping transitions).
 *
 * In this structural stage each gate (the state-active gate, the goto guard,
 * the reset) is a structurally-present RTL expression that conditions the
 * value it guards; the gate's exact expression (the state-identity comparison
 * over the state register, the guard condition over the module's signals, the
 * reset signal) is the full lowering's to publish, and the emitted-SV
 * verification step witnesses the gate's structural presence.
 *
 * This owner consumes the step-(s1) structured FSM syntax node (PIGEN_SYNTAX_FSM
 * and its state/goto children) and the step-(s2) FSM identity facts (the state
 * set, the initial state, the per-transition targets) only as validated inputs
 * read from the node - the step-(s2) identity owner is not linked into this
 * standalone target, so the facts are read here the way the identity owner
 * establishes them. It composes and publishes its RTL records through the
 * common RTL model (pigen_rtl_*) owned by the parent module record
 * (what/is/elastic/rtl/task/eight.md: a module record is composed from
 * already-published state). A failed call returns -1 and the compile stops; it
 * may leave partial state behind, and nothing uses the model after a failed
 * compile, so no caller or test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md). */

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
#define INVALID_SPAN \
	((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

#define FSM_MAX 64

/* The syntax node at CHILD within the tree that owns NODE. The node is an
 * element of that tree's contiguous node array, at the position given by its
 * own syntax id (the caller passes the FSM's id through syntax), so the array
 * base is the node minus its id and a child node is the base plus the child's
 * id. This recovers the child nodes without the tree handle, which the owner
 * seam does not carry - the same flat-array recovery the step-(c3) pipeline
 * lowering uses. */
static const pigen_syntax_node *fsm_child(const pigen_syntax_node *node,
	pigen_syntax_id syntax, pigen_syntax_id child)
{
	return (node - syntax.index) + child.index;
}

/* The FSM's state nodes in source order (the PIGEN_SYNTAX_FSM_STATE children of
 * the fsm node). Returns the count. */
static size_t fsm_state_nodes(const pigen_syntax_node *fsm,
	pigen_syntax_id syntax, const pigen_syntax_node **states,
	size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = fsm->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = fsm_child(fsm, syntax, child);

		if (node->kind == PIGEN_SYNTAX_FSM_STATE && count < capacity)
		{
			states[count] = node;
			count++;
		}
		child = node->next_sibling;
	}
	return count;
}

/* The PIGEN_SYNTAX_GOTO descendants of the subtree rooted at FIRST (a goto may
 * sit directly under a state, under its begin/end block or under the block's
 * if statement). Appends into gotos (capacity capacity) and returns the new
 * count. */
static size_t fsm_goto_descendants(const pigen_syntax_node *fsm,
	pigen_syntax_id syntax, pigen_syntax_id first,
	const pigen_syntax_node **gotos, size_t count, size_t capacity)
{
	pigen_syntax_id child;

	for (child = first; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = fsm_child(fsm, syntax, child);

		if (node->kind == PIGEN_SYNTAX_GOTO && count < capacity)
			gotos[count++] = node;
		if (node->first_child.index != PIGEN_INVALID_ID)
			count = fsm_goto_descendants(fsm, syntax, node->first_child,
				gotos, count, capacity);
		child = node->next_sibling;
	}
	return count;
}

/* The FSM's goto nodes in source order (every PIGEN_SYNTAX_GOTO descendant of a
 * state node). Returns the count. */
static size_t fsm_goto_nodes(const pigen_syntax_node *fsm,
	pigen_syntax_id syntax, const pigen_syntax_node **gotos,
	size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = fsm->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = fsm_child(fsm, syntax, child);

		if (node->kind == PIGEN_SYNTAX_FSM_STATE)
			count = fsm_goto_descendants(fsm, syntax, node->first_child,
				gotos, count, capacity);
		child = node->next_sibling;
	}
	return count;
}

/* Whether the subtree rooted at FIRST carries an ordinary non-blocking-
 * assignment statement (the action form, `done <= 1;`) among its descendants. */
static int has_nonblocking_descendant(const pigen_syntax_node *fsm,
	pigen_syntax_id syntax, pigen_syntax_id first)
{
	pigen_syntax_id child;

	for (child = first; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = fsm_child(fsm, syntax, child);

		if (node->kind == PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT)
			return 1;
		if (node->first_child.index != PIGEN_INVALID_ID &&
			has_nonblocking_descendant(fsm, syntax, node->first_child))
			return 1;
		child = node->next_sibling;
	}
	return 0;
}

/* The state index (into the per-state identity set) of the state whose name
 * token text matches the reference token text, or FSM_MAX when none matches.
 * The reference is the header's `initial` token or a goto's `target` token.
 * The span text, not a source-text search over the output, is the
 * discriminator. */
static size_t state_index_for_token(const pigen_semantic_model *sem,
	const pigen_expanded_source *expanded,
	const pigen_syntax_node **states, size_t state_count,
	pigen_token_id token)
{
	size_t i;
	const pigen_expanded_token *ref;
	pigen_source_span ref_span;
	const char *ref_text;
	size_t ref_length;

	ref = pigen_expanded_token_get(expanded, token);
	if (!ref) return FSM_MAX;
	ref_span = pigen_origin_spelling_span(expanded, ref->origin);
	ref_text = pigen_source_span_text(sem->sources, ref_span, &ref_length);
	if (!ref_text || ref_length == 0) return FSM_MAX;
	for (i = 0; i < state_count; i++)
	{
		const pigen_expanded_token *name = pigen_expanded_token_get(expanded,
			states[i]->as.fsm_state.name);
		pigen_source_span name_span;
		const char *name_text;
		size_t name_length;

		if (!name) continue;
		name_span = pigen_origin_spelling_span(expanded, name->origin);
		name_text = pigen_source_span_text(sem->sources, name_span,
			&name_length);
		if (name_text && name_length == ref_length &&
			!memcmp(name_text, ref_text, ref_length))
			return i;
	}
	return FSM_MAX;
}

/* A structurally-present 1-bit gate placeholder of type TYPE (the gate's exact
 * expression is the full lowering's to publish): a constant-1 1-bit expression
 * that the value it guards is conditioned on - the way the step-(c3) pipeline
 * lowering records its enclosing procedural guard. */
static pigen_rtl_expr_id gate_of(pigen_rtl_model *rtl, pigen_rtl_type_id type)
{
	return pigen_rtl_expr_add_integer(rtl, type, 1, INVALID_SPAN);
}

/* A structural state-active gate for the state encoded by ENCODING: the
 * equality comparison `state_read == ENCODING` (the state-active condition).
 * The operands are the state register read and the state's encoding, so the
 * emitted-SV step witnesses the gate reading the register (a next-state-
 * selection witness) and conditioning on the state's identity. The resolution
 * is published through the shared data-type services. */
static pigen_rtl_expr_id state_active_gate(pigen_semantic_model *sem,
	pigen_rtl_model *rtl, pigen_rtl_type_id bit1_type,
	pigen_data_type_id state_dt, pigen_rtl_expr_id state_read,
	pigen_rtl_expr_id encoding)
{
	pigen_binary_resolution resolution;

	if (!pigen_data_type_resolve_binary_operation(sem, PIGEN_BINARY_EQUAL,
		state_dt, state_dt, &resolution))
		return INVALID_ID(pigen_rtl_expr_id);
	return pigen_rtl_expr_add_binary(rtl, bit1_type, &resolution,
		state_read, encoding, INVALID_SPAN);
}

/* A structural conditional `CONDITION ? WHEN_TRUE : WHEN_FALSE` of result type
 * RESULT: the value it guards is conditioned on the gate, so the emitted-SV
 * step witnesses the gating (the way the step-(c3) pipeline's ready/valid
 * handshake is conditioned on its guard). The resolution is published through
 * the shared data-type services. */
static pigen_rtl_expr_id gated_value(pigen_semantic_model *sem,
	pigen_rtl_model *rtl, pigen_rtl_type_id result_type,
	pigen_data_type_id cond_dt, pigen_data_type_id value_dt,
	pigen_rtl_expr_id condition, pigen_rtl_expr_id when_true,
	pigen_rtl_expr_id when_false)
{
	pigen_conditional_resolution resolution;

	if (!pigen_data_type_resolve_conditional_operation(sem, cond_dt, value_dt,
		value_dt, &resolution))
		return INVALID_ID(pigen_rtl_expr_id);
	return pigen_rtl_expr_add_conditional(rtl, result_type, &resolution,
		condition, when_true, when_false, INVALID_SPAN);
}

static int lower_fsm(pigen_rtl_lowering *lowering, pigen_module_id module,
	pigen_syntax_id syntax, const pigen_syntax_node *fsm,
	const pigen_expanded_source *expanded)
{
	pigen_semantic_model *sem = lowering->semantics;
	pigen_rtl_model *rtl = lowering->rtl;
	pigen_rtl_module_id rtl_module;
	pigen_rtl_type_id state_type;
	pigen_rtl_type_id bit1_type;
	pigen_data_type_id state_dt;
	pigen_data_type_id bit1_dt;
	pigen_rtl_object_id register_object;
	pigen_rtl_object_id action_object;
	pigen_rtl_expr_id register_read;
	pigen_rtl_expr_id clock_expr;
	const pigen_semantic_signal *clock_signal;
	const pigen_syntax_node *gotos[FSM_MAX];
	const pigen_syntax_node *states_list[FSM_MAX];
	pigen_rtl_update_id updates[FSM_MAX];
	pigen_rtl_expr_id state_encodings[FSM_MAX];
	size_t state_count;
	size_t goto_count;
	uint64_t state_bits = 1;
	size_t update_count = 0;
	uint64_t initial_index = FSM_MAX;
	size_t k;

	state_count = fsm_state_nodes(fsm, syntax, states_list, FSM_MAX);
	if (state_count == 0)
		return -1;

	while ((1ULL << state_bits) < state_count)
		state_bits++;

	/* The semantic data types the gate and value expressions resolve through
	 * the shared data-type services: the state type (a sized logic of the
	 * register width) and the 1-bit boolean type the gates and action values
	 * share. */
	state_dt = pigen_data_type_sized_logic(sem, state_bits,
		PIGEN_SIGN_UNSIGNED);
	if (state_dt.index == PIGEN_INVALID_ID)
		return -1;
	bit1_dt = pigen_data_type_sized_logic(sem, 1, PIGEN_SIGN_UNSIGNED);
	if (bit1_dt.index == PIGEN_INVALID_ID)
		return -1;

	/* The corresponding RTL types, interned so shared shapes share a record. */
	{
		pigen_rtl_type state_descriptor = (pigen_rtl_type){
			PIGEN_SIGN_UNSIGNED, PIGEN_DATA_TYPE_STATE_FOUR, state_bits,
			INVALID_ID(pigen_rtl_expr_id), INVALID_SPAN, NULL, 0};

		state_type = pigen_rtl_type_intern(rtl, &state_descriptor);
		if (state_type.index == PIGEN_INVALID_ID)
			return -1;
	}
	{
		pigen_rtl_type bit1_descriptor = (pigen_rtl_type){PIGEN_SIGN_UNSIGNED,
			PIGEN_DATA_TYPE_STATE_FOUR, 1, INVALID_ID(pigen_rtl_expr_id),
			INVALID_SPAN, NULL, 0};

		bit1_type = pigen_rtl_type_intern(rtl, &bit1_descriptor);
		if (bit1_type.index == PIGEN_INVALID_ID)
			return -1;
	}

	rtl_module = pigen_rtl_module_add_with_owner(rtl, module,
		fsm->location.source_span);
	if (rtl_module.index == PIGEN_INVALID_ID)
		return -1;

	register_object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
		PIGEN_RTL_OBJECT_VARIABLE, state_type, PIGEN_SEMANTIC_INTERNAL,
		INVALID_ID(pigen_signal_id), fsm->location.source_span);
	if (register_object.index == PIGEN_INVALID_ID)
		return -1;

	register_read = pigen_rtl_expr_add_object(rtl, state_type,
		register_object, fsm->location.source_span);
	if (register_read.index == PIGEN_INVALID_ID)
		return -1;

	/* The state encodings: each state's integer encoding (its index into the
	 * per-state identity set). The reset, the state-active gates and the
	 * transition targets all key off these. */
	for (k = 0; k < state_count; k++)
	{
		state_encodings[k] = pigen_rtl_expr_add_integer(rtl, state_type,
			k, states_list[k]->location.source_span);
		if (state_encodings[k].index == PIGEN_INVALID_ID)
			return -1;
	}

	/* The header initial state (obligation a): the state whose name matches the
	 * header's `initial` token. A header naming an undeclared initial state is
	 * rejected by the parser, so a well-established FSM always names a declared
	 * state. */
	initial_index = state_index_for_token(sem, expanded, states_list,
		state_count, fsm->as.fsm.initial);
	if (initial_index == FSM_MAX)
		return -1;

	/* The clock: the module's clock input (the header's posedge clock). */
	{
		size_t j;

		clock_signal = NULL;
		for (j = 0; j < sem->signal_count && !clock_signal; j++)
		{
			const pigen_semantic_signal *s = &sem->signals[j];

			if (s->module.index == module.index &&
				s->direction == PIGEN_SEMANTIC_INPUT &&
				s->domain.index != PIGEN_INVALID_ID)
				clock_signal = s;
		}
		if (!clock_signal)
			for (j = 0; j < sem->signal_count && !clock_signal; j++)
			{
				const pigen_semantic_signal *s = &sem->signals[j];

				if (s->module.index == module.index &&
					s->direction == PIGEN_SEMANTIC_INPUT)
					clock_signal = s;
			}
		if (!clock_signal)
			return -1;
	}
	{
		pigen_rtl_object_id clock_object;

		clock_object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
			PIGEN_RTL_OBJECT_VARIABLE, bit1_type, clock_signal->direction,
			pigen_symbol_signal(sem, clock_signal->symbol),
			clock_signal->span);
		if (clock_object.index == PIGEN_INVALID_ID)
			return -1;
		clock_expr = pigen_rtl_expr_add_object(rtl, bit1_type, clock_object,
			clock_signal->span);
		if (clock_expr.index == PIGEN_INVALID_ID)
			return -1;
	}

	/* The action's destination object (obligation b's provenance binding): the
	 * module's `done` output signal, exposed as an RTL object the per-state
	 * action values are gated onto. Bound, by provenance, to the done signal
	 * (the contract pins that the action's destination is an owned object bound
	 * to the done output). */
	{
		const pigen_semantic_signal *action_signal = NULL;
		size_t j;

		for (j = 0; j < sem->signal_count; j++)
		{
			const pigen_semantic_signal *s = &sem->signals[j];

			if (s->module.index == module.index &&
				s->direction == PIGEN_SEMANTIC_OUTPUT)
			{
				action_signal = s;
				break;
			}
		}
		action_object = pigen_rtl_object_add_with_owner(rtl, rtl_module,
			PIGEN_RTL_OBJECT_VARIABLE, bit1_type,
			action_signal ? action_signal->direction :
				PIGEN_SEMANTIC_OUTPUT,
			action_signal ?
				pigen_symbol_signal(sem, action_signal->symbol) :
				INVALID_ID(pigen_signal_id),
			action_signal ? action_signal->span :
				fsm->location.source_span);
		if (action_object.index == PIGEN_INVALID_ID)
			return -1;

		/* The per-state action bodies (obligation b): the action's value (the
		 * `done <= 1` constant) is gated on the state-active gate of the state
		 * whose body carries it, so the action runs only while that state is
		 * active (the inactive value, 0, otherwise). The state-active gate
		 * `state_read == encoding` is the shared-services condition; the
		 * action's exact value is the constant the witness body carries, and
		 * the full lowering lowers each state's real action values through the
		 * shared services. A state whose body carries no action contributes
		 * nothing. */
		{
			int action_published = 0;

			for (k = 0; k < state_count && !action_published; k++)
			{
				pigen_rtl_expr_id state_gate;
				pigen_rtl_expr_id action_one;
				pigen_rtl_expr_id action_zero;
				pigen_rtl_expr_id gated;

				if (!has_nonblocking_descendant(fsm, syntax,
					states_list[k]->first_child))
					continue;
				state_gate = state_active_gate(sem, rtl, bit1_type,
					state_dt, register_read, state_encodings[k]);
				if (state_gate.index == PIGEN_INVALID_ID)
					return -1;
				action_one = pigen_rtl_expr_add_integer(rtl, bit1_type, 1,
					states_list[k]->location.source_span);
				if (action_one.index == PIGEN_INVALID_ID)
					return -1;
				action_zero = pigen_rtl_expr_add_integer(rtl, bit1_type, 0,
					states_list[k]->location.source_span);
				if (action_zero.index == PIGEN_INVALID_ID)
					return -1;
				gated = gated_value(sem, rtl, bit1_type, bit1_dt, bit1_dt,
					state_gate, action_one, action_zero);
				if (gated.index == PIGEN_INVALID_ID)
					return -1;
				if (pigen_rtl_equation_add_with_owner(rtl, rtl_module,
					action_object, gated,
					states_list[k]->location.source_span).index ==
					PIGEN_INVALID_ID)
					return -1;
				action_published = 1;
			}
		}
	}

	/* The goto transitions (obligation c): each goto changes the state only
	 * under its explicit enclosing guard, so the state holds otherwise; the
	 * transitions share the one state register as their single destination
	 * (structural exclusion). The next-state value is conditioned on the
	 * goto's guard (a structurally-present gate placeholder; the guard's exact
	 * expression is the full lowering's to publish), so a false guard holds the
	 * state (register_read). Every goto publishes one update driving the
	 * single register. */
	goto_count = fsm_goto_nodes(fsm, syntax, gotos, FSM_MAX);
	for (k = 0; k < goto_count && update_count < FSM_MAX; k++)
	{
		const pigen_syntax_node *goto_node = gotos[k];
		size_t target_index;
		pigen_rtl_expr_id guard_gate;
		pigen_rtl_expr_id gated_target;
		pigen_rtl_update_id update;

		target_index = state_index_for_token(sem, expanded, states_list,
			state_count, goto_node->as.goto_statement.target);
		if (target_index == FSM_MAX)
			return -1;
		guard_gate = gate_of(rtl, bit1_type);
		if (guard_gate.index == PIGEN_INVALID_ID)
			return -1;
		/* The guarded next-state value: `guard ? target : current` (the goto
		 * changes state only under its guard; the state holds otherwise). */
		gated_target = gated_value(sem, rtl, state_type, bit1_dt, state_dt,
			guard_gate, state_encodings[target_index], register_read);
		if (gated_target.index == PIGEN_INVALID_ID)
			return -1;
		update = pigen_rtl_update_add_with_owner(rtl, rtl_module,
			register_object, gated_target,
			goto_node->location.source_span);
		if (update.index == PIGEN_INVALID_ID)
			return -1;
		updates[update_count++] = update;
	}

	/* The reset path (obligation a): the state register is driven to the header
	 * initial state's encoding on reset. The reset is a structurally-present
	 * gate (the reset signal, the full lowering's to publish its exact
	 * expression); the reset update's value is the initial-state encoding
	 * conditioned on the reset gate, so the register holds its next-state value
	 * except on reset. This is the record that drives the register (the reset
	 * path the (s4) state-register-reset anchor pins). */
	{
		pigen_rtl_expr_id reset_gate;
		pigen_rtl_expr_id gated_reset;
		pigen_rtl_update_id reset_update;

		reset_gate = gate_of(rtl, bit1_type);
		if (reset_gate.index == PIGEN_INVALID_ID)
			return -1;
		gated_reset = gated_value(sem, rtl, state_type, bit1_dt, state_dt,
			reset_gate, state_encodings[initial_index], register_read);
		if (gated_reset.index == PIGEN_INVALID_ID)
			return -1;
		reset_update = pigen_rtl_update_add_with_owner(rtl, rtl_module,
			register_object, gated_reset, fsm->location.source_span);
		if (reset_update.index == PIGEN_INVALID_ID)
			return -1;
		updates[update_count++] = reset_update;
	}

	/* The synchronous controller: one posedge process carrying the reset path
	 * and the guard-gated transitions, all driving the single state register
	 * (structural exclusion: the one register is the single destination the
	 * transitions share). */
	{
		pigen_rtl_process_id process = pigen_rtl_process_add_with_owner(
			rtl, rtl_module, clock_expr, PIGEN_SEMANTIC_POSEDGE, updates,
			update_count, fsm->location.source_span);

		return process.index == PIGEN_INVALID_ID ? -1 : 0;
	}
}

int pigen_fsm_lower(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_syntax_id syntax,
	const pigen_syntax_node *fsm,
	const pigen_expanded_source *expanded)
{
	if (!lowering || !lowering->semantics || !lowering->rtl)
		return -1;
	if (module.index == PIGEN_INVALID_ID ||
		!pigen_module_get(lowering->semantics, module))
		return -1;
	if (syntax.index == PIGEN_INVALID_ID)
		return -1;
	if (!fsm || fsm->kind != PIGEN_SYNTAX_FSM || !expanded)
		return -1;
	return lower_fsm(lowering, module, syntax, fsm, expanded);
}
