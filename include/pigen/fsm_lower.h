#ifndef PIGEN_FSM_LOWER_H
#define PIGEN_FSM_LOWER_H

#include <stddef.h>

#include "pigen/ids.h"
#include "pigen/preprocess.h"
#include "pigen/rtl.h"
#include "pigen/rtl_lower.h"
#include "pigen/semantic.h"
#include "pigen/syntax.h"

/* The FSM parent-module lowering owner: the owner seam that turns one
 * PIGEN_SYNTAX_FSM statement (a module-level fsm item) into common RTL objects,
 * equations and processes of the parent module, lowering the FSM through the
 * common RTL model.
 *
 * An FSM owns a state type, one initial state, unique state identities and
 * resolved transition targets (what/is/an/fsm.md); it lowers to one synchronous
 * controller reset to its header initial state. This owner lowers that whole
 * statement so the FSM participates in the parent module's RTL exactly like any
 * other declaration or transfer, publishing into the parent module's RTL model
 * the controller's three behavioral obligations, each carried structurally
 * (a structurally-present gate expression per gate, the gate's exact
 * expression deferred to the full lowering - the way the step-(c3) pipeline
 * lowering carries its guarded ready/valid handshake):
 *
 *   - the state register: one synchronous register holding the FSM's state,
 *     reset to the header initial state (the state the `initial IDENTIFIER`
 *     names) on reset (obligation a);
 *   - the per-state action bodies: each state's ordinary statements, lowered
 *     through the shared signal/expression/guard/domain/ownership services
 *     (not private resolvers) and gated so that each state's actions run only
 *     while that state is active (obligation b);
 *   - the guard-gated goto transitions: each goto changes the FSM's state only
 *     under its explicit enclosing guard and never implicitly waits for a
 *     transfer (does/goto/wait/for/a/transfer.md); the next-state value is
 *     conditioned on the goto's guard so the state holds otherwise, and the
 *     transitions share the one state register as their single destination
 *     (obligation c, structural exclusion, how/to/write/fsm/states.md).
 *
 * The FSM's state/initial/transition identities arrive from the landed step-(s2)
 * FSM identity owner (include/pigen/fsm_identity.h + src/fsm_identity.c:
 * pigen_fsm_identity_establish / pigen_fsm_identity_is_invalid, which
 * establishes the FSM's module-scope state type, one initial state, unique
 * per-state identities and resolved per-transition targets) and the landed
 * step-(s1) structured FSM syntax node (include/pigen/syntax.h's
 * PIGEN_SYNTAX_FSM / PIGEN_SYNTAX_FSM_STATE / PIGEN_SYNTAX_GOTO, as.fsm /
 * as.fsm_state / as.goto_statement); this owner consumes them only as validated
 * inputs. It is the FSM's step-(s3) seam, mirroring the pipeline's step-(c)
 * parent-module lowering owner (include/pigen/pipeline_lower.h +
 * src/pipeline_lower.c), which turns one PIGEN_SYNTAX_PIPELINE statement into
 * the parent module's common RTL.
 *
 * This owner is a NEW structured owner on the structured path; the quarantined
 * textual prototype's include/pigen/fsm.h / src/fsm.c (pigen_lower_fsms) is a
 * different owner and is not touched, not reused, and not linked here.
 *
 * Like the pipeline lowering this owner is STANDALONE: the test target does not
 * link the full RTL lowerer (src/rtl_lower.c) or the other FSM owners. It
 * composes and publishes its RTL records through the common RTL model
 * (pigen_rtl_*), consuming the step-(s1) FSM node and the step-(s2) FSM identity
 * only as validated inputs (what/is/elastic/rtl/task/eight.md: pigen_lower_rtl_module
 * composes its module record from already-published state).
 *
 * A failed call returns -1 and the compile stops; it may leave partial state
 * behind (partial RTL objects, equations or processes in the parent module's
 * RTL model), and nothing uses the model after a failed compile, so no caller
 * or test asserts that a failure left the model unchanged
 * (how/should/compiler/builders/fail.md).
 *
 * This header fixes the seam's shape and the lowering's full-contract
 * obligations: on success it publishes the controller's owned records; on a
 * non-FSM node or an invalid argument it rejects (rc == -1) without publishing
 * anything. The step-(s5) implementation lowers the three behavioral
 * obligations structurally (a structurally-present gate expression per gate,
 * the gate's exact expression deferred to the full lowering, as the step-(c3)
 * pipeline lowering does). */

/* Lowers one PIGEN_SYNTAX_FSM statement (a module-level fsm item) through the
 * common RTL model, publishing its state register (reset to the header initial
 * state), its per-state action bodies (the ordinary statements of each state,
 * lowered through the shared signal/expression/guard/domain/ownership services
 * and gated so each state's actions run only while that state is active), and
 * its guard-gated goto transitions (a goto changes state only under its explicit
 * enclosing guard and never implicitly waits for a transfer; overlapping
 * transitions require structural exclusion), all as common RTL objects,
 * equations and processes of the parent module, as one synchronous controller.
 * lowering is the lowering context (the validated source model and the target
 * RTL model the records are published into); module is the parent module
 * identity the fsm statement lives inside; syntax is the FSM node identity
 * (PIGEN_SYNTAX_FSM); fsm is the FSM node itself, and expanded is the expanded
 * source the node was parsed from - together they carry the syntax-tree facts
 * the lowering reads (the state set, the initial state, the per-state action
 * statements, the goto transitions and their guards, and the clock and reset
 * the header names), consistent with the step-(s2) sibling
 * (pigen_fsm_identity_establish, which takes the const pigen_syntax_node *fsm).
 * On success it returns 0 and publishes the controller's owned records: the
 * state register reset to the header initial state (obligation a), the
 * per-state action bodies gated so each runs only while its state is active
 * (obligation b), and the guard-gated goto transitions whose next-state value
 * is conditioned on the goto's guard so the state holds otherwise, all sharing
 * the one state register as their single destination (obligation c, structural
 * exclusion). On error it returns -1 and reports the diagnostic and the compile
 * stops - it may leave partial state behind, and no test asserts that a failure
 * left the RTL model or the lowering maps unchanged
 * (how/should/compiler/builders/fail.md). A non-PIGEN_SYNTAX_FSM node or an
 * invalid argument is rejected (rc == -1) without publishing any RTL record -
 * the seam's own full-contract obligation. */
int pigen_fsm_lower(pigen_rtl_lowering *lowering,
	pigen_module_id module, pigen_syntax_id syntax,
	const pigen_syntax_node *fsm,
	const pigen_expanded_source *expanded);

#endif
