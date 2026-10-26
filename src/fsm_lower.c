#include <stddef.h>

#include "pigen/fsm_lower.h"

/* The FSM parent-module lowering owner (step (s3) of the FSM frontier, the
 * (s3) boundary of the FSM parent-module lowering). This file lowers one
 * PIGEN_SYNTAX_FSM statement (a module-level fsm item) through the common RTL
 * model, publishing into the parent module's RTL model, as one synchronous
 * controller:
 *
 *   - the state register: one synchronous register holding the FSM's state,
 *     reset to the header initial state on reset (what/is/an/fsm.md);
 *   - the per-state action bodies: the ordinary statements of each state,
 *     lowered through the shared signal/expression/guard/domain/ownership
 *     services (not private resolvers) and gated so each state's actions run
 *     only while that state is active (how/to/write/fsm/states.md);
 *   - the guard-gated goto transitions: each goto changes state only under its
 *     explicit enclosing guard and never implicitly waits for a transfer
 *     (does/goto/wait/for/a/transfer.md); overlapping transitions require
 *     structural exclusion.
 *
 * This owner consumes the step-(s1) structured FSM syntax node (PIGEN_SYNTAX_FSM
 * and its state/goto children) and the step-(s2) FSM identity
 * (pigen_fsm_identity_establish / pigen_fsm_identity_is_invalid) only as
 * validated inputs, and composes and publishes its RTL records through the
 * common RTL model (pigen_rtl_*) owned by the parent module record
 * (what/is/elastic/rtl/task/eight.md: a module record is composed from
 * already-published state). A failed call returns -1 and the compile stops; it
 * may leave partial state behind, and nothing uses the model after a failed
 * compile, so no caller or test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md). */

#define INVALID_ID(type) ((type){PIGEN_INVALID_ID})
/* An empty span: the invalid source id with no range. pigen_source_span is a
 * three-field struct, so it cannot be zeroed through the single-field
 * INVALID_ID macro without tripping -Wmissing-field-initializers. */
#define INVALID_SPAN \
	((pigen_source_span){INVALID_ID(pigen_source_id), 0, 0})

/* Lowers the FSM's state register, per-state action bodies and guard-gated
 * goto transitions into the parent module's RTL model, consuming the step-(s2)
 * identity and the step-(s1) node. Everything is published through the common
 * RTL model (pigen_rtl_*), owned by the parent module record. Returns 0 on
 * success and -1 on error; a failure may leave partial state behind (no
 * rollback, how/should/compiler/builders/fail.md).
 *
 * SKELETON: not implemented; the entry validates its arguments and returns -1
 * without publishing any RTL record, until the implementation stage (step (s5)). */
static int lower_fsm(pigen_rtl_lowering *lowering, pigen_module_id module,
	pigen_syntax_id syntax, const pigen_syntax_node *fsm,
	const pigen_expanded_source *expanded)
{
	(void)lowering;
	(void)module;
	(void)syntax;
	(void)fsm;
	(void)expanded;
	return -1;
}

int pigen_fsm_lower(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_syntax_id syntax,
	const pigen_syntax_node *fsm,
	const pigen_expanded_source *expanded)
{
	/* Validate the lowering context, the parent module and the FSM node
	 * identity, exactly as the sibling lowerer seam validates its context
	 * (src/rtl_lower.c, pigen_lower_rtl_transfers; src/pipeline_lower.c,
	 * pigen_pipeline_lower). */
	if (!lowering || !lowering->semantics || !lowering->rtl)
		return -1;
	if (module.index == PIGEN_INVALID_ID ||
		!pigen_module_get(lowering->semantics, module))
		return -1;
	if (syntax.index == PIGEN_INVALID_ID)
		return -1;
	/* The seam carries the syntax tree the lowering reads: the FSM node and the
	 * expanded source it was parsed from. Validate them and reject a node that
	 * is not a PIGEN_SYNTAX_FSM statement (the seam's own full-contract
	 * obligation - a well-established FSM with a legal fsm node must lower, so a
	 * non-FSM node must be the form the lowering rejects). */
	if (!fsm || fsm->kind != PIGEN_SYNTAX_FSM || !expanded)
		return -1;
	return lower_fsm(lowering, module, syntax, fsm, expanded);
}
