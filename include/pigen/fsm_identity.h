#ifndef PIGEN_FSM_IDENTITY_H
#define PIGEN_FSM_IDENTITY_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* The FSM identity owner: the owner seam that turns one parsed FSM (a
 * PIGEN_SYNTAX_FSM node) into the FSM's module-scope identity in the resolved
 * semantic model.
 *
 * An FSM owns a state type, one initial state, unique state identities and
 * resolved transition targets (what/is/an/fsm.md): the state type is an
 * enum-like type over the FSM's declared states, the one initial state is the
 * state the header names (`initial IDENTIFIER`), each state identity is the
 * unique identity of one declared state, and each resolved transition target
 * is the target state of one goto (each PIGEN_SYNTAX_GOTO's target state).
 * This owner is the FSM's step-(s2) seam, mirroring the pipeline's step-(a)
 * outgoing-packet identity owner (include/pigen/pipeline_identity.h +
 * src/pipeline_identity.c): that owner establishes a pipeline's module-scope
 * buf output packet identity, and this owner establishes an FSM's module-scope
 * state type, initial state, per-state identities and per-transition targets.
 *
 * A failed call returns -1 and the compile stops; it may leave partial state
 * behind, and nothing uses the model after a failed compile, so no caller or
 * test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md).
 *
 * This seam owns the FSM identity on the structured path
 * (include/pigen/fsm_identity.h + src/fsm_identity.c); the quarantined textual
 * prototype's include/pigen/fsm.h / src/fsm.c (pigen_lower_fsms) is a different
 * owner and is not touched, reused or linked. */

/* The established module-scope identity of one FSM. state_type is the FSM's
 * state type, an enum-like type over its declared states; initial_state is the
 * identity of the FSM's one initial state (the state the header names);
 * first_state / state_count are the FSM's per-state identity set, the syntax
 * ids of the PIGEN_SYNTAX_FSM_STATE nodes in source order (one unique identity
 * per declared state); first_transition / transition_count are the FSM's
 * per-transition target set, the syntax ids of the PIGEN_SYNTAX_GOTO nodes in
 * source order (one resolved transition target per goto); span is the FSM's
 * original span (the owning node's source span); syntax is the owning FSM
 * node's syntax id (the PIGEN_SYNTAX_FSM node). An invalid identity carries
 * the PIGEN_INVALID_ID sentinel in state_type, initial_state, first_state and
 * first_transition, a state_count of 0, a transition_count of 0, an empty span
 * and an invalid syntax id. */
typedef struct {
	pigen_data_type_id state_type;
	pigen_symbol_id initial_state;
	pigen_syntax_id first_state;
	size_t state_count;
	pigen_syntax_id first_transition;
	size_t transition_count;
	pigen_source_span span;
	pigen_syntax_id syntax;
} pigen_fsm_identity;

/* Tests whether *identity is an invalid FSM identity (the PIGEN_INVALID_ID
 * sentinel in its state_type, initial_state, first_state and first_transition
 * ids, a state_count of 0, a transition_count of 0, an empty span and an
 * invalid syntax id). Returns 1 when the identity is invalid and 0 otherwise. */
int pigen_fsm_identity_is_invalid(const pigen_fsm_identity *identity);

/* Establishes the FSM's module-scope identity in the model. fsm is the parsed
 * FSM node (PIGEN_SYNTAX_FSM) and its id; module is the resolved module the
 * FSM lives in; expanded is the parsed expanded source the FSM node was parsed
 * from (the source that resolves the FSM's token origins to their original
 * spelling spans, as the syntax tree itself does through its expanded source).
 * On success it establishes the FSM's state type (an enum-like type over its
 * declared states), its one initial state, its unique per-state identities and
 * its resolved per-transition targets, then fills *identity. Returns 0 on
 * success and -1 on error; a failed call reports the error and the compile
 * stops - it may leave partial state behind, and no test asserts that a
 * failure left the model unchanged. SKELETON: validates its arguments and
 * returns -1, leaving *identity invalid, until the implementation stage. */
int pigen_fsm_identity_establish(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *fsm,
	const pigen_expanded_source *expanded,
	pigen_fsm_identity *identity);

#endif
