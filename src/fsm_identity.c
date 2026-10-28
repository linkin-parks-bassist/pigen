#include <stddef.h>
#include <string.h>

#include "pigen/fsm_identity.h"
#include "pigen/data_type.h"
#include "pigen/preprocess.h"

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
/* An empty span: the invalid source id with no range. pigen_source_span is a
 * three-field struct, so it cannot be zeroed through the single-field
 * INVALID_ID macro without tripping -Wmissing-field-initializers. */
#define INVALID_SPAN ((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

/* The tree node at CHILD, recovered from the FSM node's position in the tree's
 * contiguous node array: the FSM node is the element at the position given by
 * its own syntax id, so the array base is the FSM node minus its id and a child
 * node is the base plus the child's id (the flat-array recovery the sibling
 * owners use). */
static const pigen_syntax_node *fsm_node(const pigen_syntax_node *fsm,
	pigen_syntax_id syntax, pigen_syntax_id child)
{
	return (fsm - syntax.index) + child.index;
}

/* The FSM's state nodes in source order (its PIGEN_SYNTAX_FSM_STATE children)
 * into out (capacity capacity). Returns the count. */
static size_t fsm_state_nodes(const pigen_syntax_node *fsm,
	pigen_syntax_id syntax, const pigen_syntax_node **states,
	size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = fsm->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = fsm_node(fsm, syntax, child);

		if (node->kind == PIGEN_SYNTAX_FSM_STATE && count < capacity)
		{
			states[count] = node;
			count++;
		}
		child = node->next_sibling;
	}
	return count;
}

/* The FSM's goto nodes in source order (every PIGEN_SYNTAX_GOTO descendant of a
 * state node; a goto may sit directly under a state, under its begin/end block
 * or under the block's if statement) into out (capacity capacity). Returns the
 * count. */
static size_t fsm_goto_descendants(const pigen_syntax_node *fsm,
	pigen_syntax_id syntax, pigen_syntax_id first,
	const pigen_syntax_node **gotos, size_t count, size_t capacity)
{
	pigen_syntax_id child;

	for (child = first; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = fsm_node(fsm, syntax, child);

		if (node->kind == PIGEN_SYNTAX_GOTO && count < capacity)
			gotos[count++] = node;
		if (node->first_child.index != PIGEN_INVALID_ID)
			count = fsm_goto_descendants(fsm, syntax, node->first_child,
				gotos, count, capacity);
		child = node->next_sibling;
	}
	return count;
}

static size_t fsm_goto_nodes(const pigen_syntax_node *fsm,
	pigen_syntax_id syntax, const pigen_syntax_node **gotos,
	size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = fsm->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = fsm_node(fsm, syntax, child);

		if (node->kind == PIGEN_SYNTAX_FSM_STATE)
			count = fsm_goto_descendants(fsm, syntax, node->first_child,
				gotos, count, capacity);
		child = node->next_sibling;
	}
	return count;
}

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

/* Establishes the FSM's module-scope identity in the model (what/is/an/fsm.md):
 * the state type (an enum-like type over the FSM's declared states, a sized
 * logic wide enough to encode every state), the one initial state (the state
 * the header's `initial IDENTIFIER` names), the unique per-state identity set
 * (the syntax ids of the PIGEN_SYNTAX_FSM_STATE nodes in source order, one
 * unique identity per declared state) and the resolved per-transition target
 * set (the syntax ids of the PIGEN_SYNTAX_GOTO nodes in source order, one
 * resolved transition target per goto). Fills *identity and returns 0 on
 * success. On error it returns -1, leaves *identity invalid and the compile
 * stops - it may leave partial state behind, and no test asserts that a
 * failure left the model unchanged (how/should/compiler/builders/fail.md). */
int pigen_fsm_identity_establish(pigen_semantic_model *model,
	pigen_syntax_id syntax, pigen_module_id module,
	const pigen_syntax_node *fsm,
	const pigen_expanded_source *expanded,
	pigen_fsm_identity *identity)
{
	const pigen_syntax_node *states[64];
	const pigen_syntax_node *gotos[64];
	pigen_data_type_id state_type;
	size_t state_count;
	size_t transition_count;
	size_t initial_state = PIGEN_INVALID_ID;
	size_t i;

	*identity = (pigen_fsm_identity){
		INVALID_ID(pigen_data_type_id), INVALID_ID(pigen_symbol_id),
		INVALID_ID(pigen_syntax_id), 0,
		INVALID_ID(pigen_syntax_id), 0,
		INVALID_SPAN, INVALID_ID(pigen_syntax_id)
	};
	/* Validate the arguments: a non-null model and identity, the expanded
	 * source the FSM node was parsed from, and a parsed FSM node (not some
	 * other node kind). A non-FSM node kind is rejected - the owner seam
	 * consumes only a PIGEN_SYNTAX_FSM. */
	if (!model || !identity || !expanded || !fsm ||
		fsm->kind != PIGEN_SYNTAX_FSM)
		return -1;
	(void)module;

	/* The declared states in source order: the per-state identity set is the
	 * FSM's first state (its first PIGEN_SYNTAX_FSM_STATE child) and its state
	 * count (the number of PIGEN_SYNTAX_FSM_STATE children). An FSM always
	 * declares at least one state (the parser requires the header's initial
	 * state to be a declared state). */
	state_count = fsm_state_nodes(fsm, syntax, states, 64);
	if (state_count == 0)
		return -1;

	/* The FSM's one initial state (the state the header names): the per-state
	 * identity of the declared state whose name token text matches the header's
	 * `initial IDENTIFIER` token text. A header that names an undeclared initial
	 * state is a failure the parser already rejects, so a well-established FSM
	 * always names a declared state. */
	{
		const pigen_expanded_token *initial_token =
			pigen_expanded_token_get(expanded, fsm->as.fsm.initial);
		pigen_source_span initial_span;
		const char *initial_text;
		size_t initial_length;

		if (!initial_token)
			return -1;
		initial_span = pigen_origin_spelling_span(expanded,
			initial_token->origin);
		initial_text = pigen_source_span_text(model->sources, initial_span,
			&initial_length);
		if (!initial_text || initial_length == 0)
			return -1;
		for (i = 0; i < state_count; i++)
		{
			const pigen_expanded_token *name_token =
				pigen_expanded_token_get(expanded,
					states[i]->as.fsm_state.name);
			pigen_source_span name_span;
			const char *name_text;
			size_t name_length;

			if (!name_token)
				continue;
			name_span = pigen_origin_spelling_span(expanded,
				name_token->origin);
			name_text = pigen_source_span_text(model->sources, name_span,
				&name_length);
			if (name_text && name_length == initial_length &&
				!memcmp(name_text, initial_text, initial_length))
			{
				initial_state = i;
				break;
			}
		}
		if (initial_state == PIGEN_INVALID_ID)
			return -1;
	}

	/* The state type: an enum-like type over the declared states, a sized logic
	 * wide enough to encode every state (a single bit for one or two states,
	 * otherwise enough bits for state_count values). */
	{
		uint64_t bits = 1;
		uint64_t values = 1;

		while (values < (uint64_t)state_count)
		{
			bits++;
			values *= 2;
		}
		state_type = pigen_data_type_sized_logic(model, bits,
			PIGEN_SIGN_UNSIGNED);
		if (state_type.index == PIGEN_INVALID_ID)
			return -1;
	}

	/* The resolved per-transition target set: the syntax ids of the
	 * PIGEN_SYNTAX_GOTO nodes in source order (a goto may forward-reference a
	 * later state; the parser has already checked every goto target is a
	 * declared state, so the target set is the goto nodes in source order). */
	transition_count = fsm_goto_nodes(fsm, syntax, gotos, 64);

	/* Fill the established identity: the state type, the one initial state (the
	 * per-state identity of the declared state the header names, carried as the
	 * initial state's index into the per-state identity set), the per-state
	 * identity set (first_state is the FSM's first state child), the
	 * per-transition target set (first_transition is the first goto), the FSM's
	 * original span and its owning node id. */
	identity->state_type = state_type;
	identity->initial_state = (pigen_symbol_id){(uint32_t)initial_state};
	identity->first_state = (pigen_syntax_id){states[0] - (fsm - syntax.index)};
	identity->state_count = state_count;
	identity->first_transition = transition_count ?
		(pigen_syntax_id){gotos[0] - (fsm - syntax.index)} :
		INVALID_ID(pigen_syntax_id);
	identity->transition_count = transition_count;
	identity->span = fsm->location.source_span;
	identity->syntax = syntax;
	return 0;
}
