#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pigen/fsm_identity.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* A legal inline FSM on the structured path supplies the PIGEN_SYNTAX_FSM node
 * the owner seam consumes. Plain logic ports keep the witness minimal; the data
 * type precedes the transfer type on the structured path, and an FSM needs only
 * a posedge clock and a reset signal. The state bodies are single goto
 * statements, legal on their own syntactic path. */
#define FSM_WITNESS \
	"module fsm_witness(input logic clk, input logic reset);\n" \
	"\tfsm sender @(posedge clk) reset (reset) initial idle\n" \
	"\tbegin\n" \
	"\t\tstate idle:\n" \
	"\t\tbegin\n" \
	"\t\t\tgoto run;\n" \
	"\t\tend\n" \
	"\t\tstate run:\n" \
	"\t\tbegin\n" \
	"\t\t\tgoto idle;\n" \
	"\t\tend\n" \
	"\tend\n" \
	"endmodule"

/* The discriminating initial-state witness: the same minimal structured-path
 * FSM as FSM_WITNESS, but the header names `run`, the SECOND declared state,
 * while `idle` is declared first. The owner contract
 * (include/pigen/fsm_identity.h) makes initial_state the identity of the state
 * the header names (what/is/an/fsm.md: the controller resets to the header
 * initial state), so on this witness the correct identity carries
 * initial_state.index == 1; an owner that ignores the header's initial token
 * and always picks the first declared state returns 0 and cannot pass. */
#define FSM_WITNESS_NONFIRST \
	"module fsm_witness_nonfirst(input logic clk, input logic reset);\n" \
	"\tfsm sender @(posedge clk) reset (reset) initial run\n" \
	"\tbegin\n" \
	"\t\tstate idle:\n" \
	"\t\tbegin\n" \
	"\t\t\tgoto run;\n" \
	"\t\tend\n" \
	"\t\tstate run:\n" \
	"\t\tbegin\n" \
	"\t\t\tgoto idle;\n" \
	"\t\tend\n" \
	"\tend\n" \
	"endmodule"

typedef struct {
	pigen_source_id source;
	pigen_preprocess_result preprocessed;
	pigen_syntax_tree tree;
	pigen_syntax_error error;
} parsed_fixture;

static int parse_fixture(pigen_source_manager *sources, const char *name,
	const char *text, parsed_fixture *fixture)
{
	pigen_source_id source = pigen_source_add(sources, name, text, strlen(text));
	pigen_preprocess_error preprocess_error = {0};

	assert(source.index != PIGEN_INVALID_ID);
	fixture->source = source;
	assert(pigen_preprocess(sources, source, NULL, &fixture->preprocessed,
		&preprocess_error));
	return pigen_parse_syntax(&fixture->preprocessed.expanded, &fixture->tree,
		&fixture->error);
}

static void free_fixture(parsed_fixture *fixture)
{
	pigen_free_syntax_tree(&fixture->tree);
	pigen_free_preprocess_result(&fixture->preprocessed);
	*fixture = (parsed_fixture){0};
}

static const pigen_syntax_node *fixture_module(const parsed_fixture *fixture)
{
	const pigen_syntax_node *root = pigen_syntax_get(&fixture->tree,
		(pigen_syntax_id){0});
	pigen_syntax_id child;

	assert(root && root->kind == PIGEN_SYNTAX_COMPILATION_UNIT);
	for (child = root->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_MODULE) return node;
		child = node->next_sibling;
	}
	return NULL;
}

/* An FSM is a module-level item, a direct child of the module node. */
static const pigen_syntax_node *find_fsm(const parsed_fixture *fixture)
{
	const pigen_syntax_node *module = fixture_module(fixture);
	pigen_syntax_id child;

	assert(module);
	for (child = module->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);
		assert(node);
		if (node->kind == PIGEN_SYNTAX_FSM) return node;
		child = node->next_sibling;
	}
	return NULL;
}

/* The syntax id of a node: its position in the tree's contiguous node array. */
static pigen_syntax_id syntax_id_of(const pigen_syntax_tree *tree,
	const pigen_syntax_node *node)
{
	return (pigen_syntax_id){(uint32_t)(node - tree->nodes)};
}

/* The FSM's state nodes in source order (its PIGEN_SYNTAX_FSM_STATE children)
 * into out (capacity capacity). Returns the count. */
static size_t collect_states(const parsed_fixture *fixture,
	const pigen_syntax_node *fsm,
	const pigen_syntax_node **out, size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = fsm->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);

		assert(node);
		if (node->kind == PIGEN_SYNTAX_FSM_STATE && count < capacity)
			out[count++] = node;
		child = node->next_sibling;
	}
	return count;
}

/* The FSM's goto nodes in source order (every PIGEN_SYNTAX_GOTO descendant of
 * a state node, which may sit directly under a state, under its begin/end
 * block or under the block's if statement) into out (capacity capacity).
 * Returns the count. */
static size_t collect_gotos_in(const parsed_fixture *fixture,
	pigen_syntax_id first, const pigen_syntax_node **out, size_t count,
	size_t capacity)
{
	pigen_syntax_id child;

	for (child = first; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);

		assert(node);
		if (node->kind == PIGEN_SYNTAX_GOTO && count < capacity)
			out[count++] = node;
		if (node->first_child.index != PIGEN_INVALID_ID)
			count = collect_gotos_in(fixture, node->first_child, out, count,
				capacity);
		child = node->next_sibling;
	}
	return count;
}

static size_t collect_fsm_gotos(const parsed_fixture *fixture,
	const pigen_syntax_node *fsm, const pigen_syntax_node **out,
	size_t capacity)
{
	size_t count = 0;
	pigen_syntax_id child;

	for (child = fsm->first_child; child.index != PIGEN_INVALID_ID; )
	{
		const pigen_syntax_node *node = pigen_syntax_get(&fixture->tree, child);

		assert(node);
		if (node->kind == PIGEN_SYNTAX_FSM_STATE)
			count = collect_gotos_in(fixture, node->first_child, out, count,
				capacity);
		child = node->next_sibling;
	}
	return count;
}

/* The all-sentinel identity: PIGEN_INVALID_ID in every id, zero counts and an
 * empty span, exactly what the owner seam leaves behind on a failed call. */
static int identity_is_all_sentinel(const pigen_fsm_identity *identity)
{
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

/* (1) establish-valid-args: the discriminating validation witness. A valid
 * PIGEN_SYNTAX_FSM node is accepted: establish returns 0 and fills a real,
 * non-sentinel identity (a real state type id, the initial-state identity,
 * the per-state identity count, the per-transition target count, a non-empty
 * span and the owning syntax node id). The initial-state identity is pinned
 * to the state the header names: on the witness whose header names the second
 * declared state it must be that state's per-state identity (index 1), so an
 * establish that ignores the header's initial token and always picks the
 * first declared state cannot pass. A non-FSM node kind and each NULL
 * argument are rejected rc == -1 leaving *identity all-sentinel, so an
 * establish that skips its validation cannot pass this section: the valid
 * node demands a success path the skeleton has no path to. */
static void test_fsm_establish_valid_args(pigen_source_manager *sources)
{
	parsed_fixture fixture = {0};
	parsed_fixture nonfirst = {0};
	pigen_semantic_model model;
	pigen_fsm_identity identity = {0};
	const pigen_syntax_node *states[64];
	const pigen_syntax_node *gotos[64];
	const pigen_syntax_node *nonfirst_states[64];
	const pigen_syntax_node *nonfirst_gotos[64];
	const pigen_syntax_node *fsm;
	const pigen_syntax_node *nonfirst_fsm;
	pigen_syntax_id syntax;
	pigen_syntax_id nonfirst_syntax;
	size_t state_count, goto_count;
	size_t nonfirst_state_count, nonfirst_goto_count;
	pigen_module_id module = {PIGEN_INVALID_ID};

	assert(parse_fixture(sources, "fsm_witness.pigen", FSM_WITNESS, &fixture));
	assert(!fixture.error.message);
	fsm = find_fsm(&fixture);
	assert(fsm);
	assert(fsm->kind == PIGEN_SYNTAX_FSM);
	syntax = syntax_id_of(&fixture.tree, fsm);
	state_count = collect_states(&fixture, fsm, states, 64);
	goto_count = collect_fsm_gotos(&fixture, fsm, gotos, 64);
	assert(state_count == 2);
	assert(goto_count == 2);

	/* The individually-invalid forms are rejected rc == -1 leaving
	 * *identity all-sentinel: a non-FSM node kind (the owning module node is
	 * a valid parsed node, but not the node kind this owner consumes), a NULL
	 * model and a NULL expanded source. */
	{
		const pigen_syntax_node *wrong = fixture_module(&fixture);
		assert(wrong && wrong->kind != PIGEN_SYNTAX_FSM);
		pigen_semantic_init(&model, sources);
		assert(pigen_fsm_identity_establish(&model, syntax, module, wrong,
			&fixture.preprocessed.expanded, &identity) == -1);
		assert(identity_is_all_sentinel(&identity));
		pigen_free_semantic_model(&model);
	}
	pigen_semantic_init(&model, sources);
	assert(pigen_fsm_identity_establish(NULL, syntax, module, fsm,
		&fixture.preprocessed.expanded, &identity) == -1);
	assert(identity_is_all_sentinel(&identity));
	pigen_free_semantic_model(&model);
	pigen_semantic_init(&model, sources);
	assert(pigen_fsm_identity_establish(&model, syntax, module, fsm, NULL,
		&identity) == -1);
	assert(identity_is_all_sentinel(&identity));
	pigen_free_semantic_model(&model);

	/* The valid form: non-null model, identity, expanded source and the
	 * PIGEN_SYNTAX_FSM node. establish returns 0 and fills a real,
	 * non-sentinel identity. */
	identity = (pigen_fsm_identity){0};
	pigen_semantic_init(&model, sources);
	assert(pigen_fsm_identity_establish(&model, syntax, module, fsm,
		&fixture.preprocessed.expanded, &identity) == 0);
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);

	/* A real state type id. */
	assert(identity.state_type.index != PIGEN_INVALID_ID);

	/* The initial-state identity: a real id. */
	assert(identity.initial_state.index != PIGEN_INVALID_ID);

	/* The per-state identity set: the syntax ids of the PIGEN_SYNTAX_FSM_STATE
	 * nodes in source order (idle then run for the witness). */
	assert(identity.state_count == state_count);
	assert(identity.first_state.index ==
		syntax_id_of(&fixture.tree, states[0]).index);

	/* The per-transition target set: the syntax ids of the PIGEN_SYNTAX_GOTO
	 * nodes in source order (idle's goto then run's for the witness). */
	assert(identity.transition_count == goto_count);
	assert(identity.first_transition.index ==
		syntax_id_of(&fixture.tree, gotos[0]).index);

	/* The FSM's original span: non-empty and over the witness source. */
	assert(identity.span.source.index == fixture.source.index);
	assert(identity.span.start < identity.span.end);

	/* The owning syntax node id: the PIGEN_SYNTAX_FSM node itself. */
	assert(identity.syntax.index == syntax.index);

	pigen_free_semantic_model(&model);
	free_fixture(&fixture);

	/* The discriminating initial-state witness: the header names `run`, the
	 * second declared state (idle is declared first). establish returns 0 and
	 * the initial-state identity is the per-state identity of the state the
	 * header names - index 1 into the per-state set - so an owner that always
	 * picks the first declared state (index 0) fails this assert. */
	assert(parse_fixture(sources, "fsm_witness_nonfirst.pigen",
		FSM_WITNESS_NONFIRST, &nonfirst));
	assert(!nonfirst.error.message);
	nonfirst_fsm = find_fsm(&nonfirst);
	assert(nonfirst_fsm);
	assert(nonfirst_fsm->kind == PIGEN_SYNTAX_FSM);
	nonfirst_syntax = syntax_id_of(&nonfirst.tree, nonfirst_fsm);
	nonfirst_state_count = collect_states(&nonfirst, nonfirst_fsm,
		nonfirst_states, 64);
	nonfirst_goto_count = collect_fsm_gotos(&nonfirst, nonfirst_fsm,
		nonfirst_gotos, 64);
	assert(nonfirst_state_count == 2);
	assert(nonfirst_goto_count == 2);

	identity = (pigen_fsm_identity){0};
	pigen_semantic_init(&model, sources);
	assert(pigen_fsm_identity_establish(&model, nonfirst_syntax, module,
		nonfirst_fsm, &nonfirst.preprocessed.expanded, &identity) == 0);
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);

	/* A real state type id. */
	assert(identity.state_type.index != PIGEN_INVALID_ID);

	/* The discriminating initial-state pin: the header names the second
	 * declared state, so its per-state identity is index 1, not the first
	 * declared state's index 0. */
	assert(identity.initial_state.index == 1);

	/* The per-state identity set: idle first, then run. */
	assert(identity.state_count == nonfirst_state_count);
	assert(identity.first_state.index ==
		syntax_id_of(&nonfirst.tree, nonfirst_states[0]).index);

	/* The per-transition target set: idle's goto first, then run's. */
	assert(identity.transition_count == nonfirst_goto_count);
	assert(identity.first_transition.index ==
		syntax_id_of(&nonfirst.tree, nonfirst_gotos[0]).index);

	/* The FSM's original span: non-empty and over the witness source. */
	assert(identity.span.source.index == nonfirst.source.index);
	assert(identity.span.start < identity.span.end);

	/* The owning syntax node id: the PIGEN_SYNTAX_FSM node itself. */
	assert(identity.syntax.index == nonfirst_syntax.index);

	pigen_free_semantic_model(&model);
	free_fixture(&nonfirst);
}

/* (2) is-invalid-sentinel: the predicate is 1 for the all-sentinel identity and
 * 0 once any single field is set; a null pointer reports 1. */
static void test_fsm_is_invalid_sentinel(pigen_source_manager *sources)
{
	pigen_fsm_identity identity = {
		{PIGEN_INVALID_ID}, {PIGEN_INVALID_ID}, {PIGEN_INVALID_ID}, 0,
		{PIGEN_INVALID_ID}, 0, {{PIGEN_INVALID_ID}, 0, 0}, {PIGEN_INVALID_ID}
	};

	(void)sources;
	assert(identity_is_all_sentinel(&identity));
	assert(pigen_fsm_identity_is_invalid(&identity) == 1);
	assert(pigen_fsm_identity_is_invalid(NULL) == 1);

	/* Setting any single id clears the sentinel. */
	identity.state_type.index = 0;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.state_type.index = PIGEN_INVALID_ID;

	identity.initial_state.index = 0;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.initial_state.index = PIGEN_INVALID_ID;

	identity.first_state.index = 0;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.first_state.index = PIGEN_INVALID_ID;

	identity.first_transition.index = 0;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.first_transition.index = PIGEN_INVALID_ID;

	/* Setting any single count clears the sentinel. */
	identity.state_count = 1;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.state_count = 0;

	identity.transition_count = 1;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.transition_count = 0;

	/* Setting the owning syntax id clears the sentinel. */
	identity.syntax.index = 0;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.syntax.index = PIGEN_INVALID_ID;

	/* A non-empty span clears the sentinel. */
	identity.span.source.index = 0;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.span.source.index = PIGEN_INVALID_ID;

	identity.span.start = 1;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.span.start = 0;

	identity.span.end = 1;
	assert(pigen_fsm_identity_is_invalid(&identity) == 0);
	identity.span.end = 0;
}

int main(int argc, char **argv)
{
	pigen_source_manager sources = {0};

	if (argc > 1 && strcmp(argv[1], "establish-valid-args") == 0)
		test_fsm_establish_valid_args(&sources);
	else if (argc > 1 && strcmp(argv[1], "is-invalid-sentinel") == 0)
		test_fsm_is_invalid_sentinel(&sources);
	else
	{
		fprintf(stderr, "usage: fsm-identity-test "
			"{establish-valid-args|is-invalid-sentinel}\n");
		return 2;
	}
	pigen_free_sources(&sources);
	printf("PASS: fsm-identity-test %s\n", argv[1]);
	return 0;
}
