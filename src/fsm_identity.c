#include "pigen/fsm_identity.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
/* An empty span: the invalid source id with no range. pigen_source_span is a
 * three-field struct, so it cannot be zeroed through the single-field
 * INVALID_ID macro without tripping -Wmissing-field-initializers. */
#define INVALID_SPAN ((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

/* Tests whether *identity is an invalid FSM identity: the PIGEN_INVALID_ID
 * sentinel in its state_type, initial_state, first_state and first_transition
 * ids, a state_count of 0, a transition_count of 0, an empty span and an
 * invalid syntax id. */
int pigen_fsm_identity_is_invalid(const pigen_fsm_identity *identity)
{
	if (!identity) return 1;
	return identity->state_type.index == PIGEN_INVALID_ID &&
		identity->initial_state.index == PIGEN_INVALID_ID &&
		identity->first_state.index == PIGEN_INVALID_ID &&
		identity->state_count == 0 &&
		identity->first_transition.index == PIGEN_INVALID_ID &&
		identity->transition_count == 0 &&
		identity->span.source.index == PIGEN_INVALID_ID &&
		identity->span.start == 0 &&
		identity->span.end == 0 &&
		identity->syntax.index == PIGEN_INVALID_ID;
}

/* Establishes the FSM's module-scope identity in the model.
 *
 * SKELETON: the entry validates its arguments and returns -1, leaving
 * *identity invalid, until the implementation stage (what is/is/the/plan.md
 * step (s2): the FSM identity owner). On success the implementation will
 * establish the FSM's state type (an enum-like type over its declared
 * states), its one initial state, its unique per-state identities and its
 * resolved per-transition targets, then fill *identity. A failed call reports
 * the error and the compile stops - it may leave partial state behind, and no
 * test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md). */
int pigen_fsm_identity_establish(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *fsm,
	const pigen_expanded_source *expanded,
	pigen_fsm_identity *identity)
{
	*identity = (pigen_fsm_identity){
		INVALID_ID(pigen_data_type_id), INVALID_ID(pigen_symbol_id),
		INVALID_ID(pigen_syntax_id), 0,
		INVALID_ID(pigen_syntax_id), 0,
		INVALID_SPAN, INVALID_ID(pigen_syntax_id)
	};
	/* Validate the arguments: a non-null model and identity, the expanded
	 * source the FSM node was parsed from, and a parsed FSM node (not some
	 * other node kind). Invalid arguments are rejected before any identity
	 * is established. */
	if (!model || !identity || !expanded || !fsm ||
		fsm->kind != PIGEN_SYNTAX_FSM)
		return -1;
	/* SKELETON: the FSM identity is not implemented yet; leave the identity
	 * invalid and fail (how/should/compiler/builders/fail.md). */
	(void)syntax;
	(void)module;
	return -1;
}
