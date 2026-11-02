---
status: green
revised_at: "2026-11-03T10:55:19+11:00"
---

## Scope

The user-requested pipeline/FSM behavioral test contracts, decomposed into three scoped obligations: (1) pipeline stage computation, (2) pipeline halting/stop (guarded enable), (3) FSM behavior/stop. CLI reachability of pipeline/FSM statements is a separate integration concern (does/the/pipeline/and/fsm/reach/the/compiled/output.md). Each obligation names its current test coverage.

## 1. Pipeline stage computation — landed on master, green

Contract: each stage's `<=` computes the outgoing packet as expressions over the immutable incoming fields and enclosing signals (how/do/pipeline/stage/reads/work.md). Coverage: src/pipeline_lower.c publishes each stage's `<=` computation as its next-value equation (a structural expression; a stage with no write publishes no equation), and pipeline-lower-test pins it with the six sections calc-arithmetic, calc-bitwise, calc-first-stage, calc-cross-boundary, calc-no-standin and calc-concat (mechanism at what/are/the/pipeline/stage/computation/test/scenarios.md; accepted review at what/is/the/review/of/the/pipeline/stage/computation/test/scenarios.md).

## 2. Pipeline halting/stop — landed on master, green

Contract: the complete enclosing procedural guard enables the pipeline; false holds all storage and disables input/output handshakes; pipe_reset/pipeline_reset guard bindings directly reset every generated valid bit (how/to/reset/and/enable/a/pipeline.md). Coverage: pipeline-lower-test pins the ready *value* with the six sections named in what/are/the/pipeline/halt/stop/test/scenarios.md (the ready value carries the complete guard, is not the constant-1 stand-in, stays constant-1 when unguarded, publishes the compound guard, and renders the spelled guard) and the storage-hold / I/O-handshake *effects* with the eight guard storage/io sections (each per-stage next value is the guard-gated hold mux, the stage-boundary buf storage publishes its guard connection, the yield write carries the guard, the unguarded pipeline publishes no guard object and no gated write, both render the spelled guard) at what/are/the/pipeline/guard/storage/io/test/scenarios.md — the guard is wired into the storage update and both handshakes, so a false guard is enforced in the published model (disposition at how/is/the/pipeline/guard/law/enforced/by/the/published/model.md). Both lists passed their independent reviews. Reset-binding coverage follows in its own scoped mapping.

## 3. FSM behavior/stop — landed on master, green

Contract: one synchronous controller reset to its header initial state; each goto changes state only under its explicit enclosing guard and never implicitly waits for a transfer; the state holds otherwise (what/is/an/fsm.md, does/goto/wait/for/a/transfer.md, how/to/write/fsm/states.md). Coverage: the five discriminating sections (reset-value-initial-state, state-active-action-gate, goto-guard-gates-next-state, goto-never-waits-for-transfer, one-synchronous-controller) are landed on the fsm-lower-test manifest alongside the five (s4) base sections, so make fsm-lower-test runs ten sections green under -Werror; they pin, as published record structure, the reset value (the header initial state's encoding, via a non-first witness), the state-active gate and the per-goto guard gating (what/is/the/fsm/parent/module/lowering.md, step (s4b)). The gate expressions are witnessed as published record structure - pigen_rtl_update.value and pigen_rtl_equation.value are pigen_rtl_expr trees and a conditional's kind, integer value, object reference and ordered children [condition, when_true, when_false] are readable via pigen_rtl_expr_get / pigen_rtl_expr_children (include/pigen/rtl.h) - with a file-local non-first initial-state witness for the reset value. The list is at what/are/the/fsm/behavior/stop/test/scenarios.md, accepted by the independent review at what/is/the/review/of/the/fsm/behavior/stop/test/scenarios.md (plan item 8 now COMPLETE).

## Continuation

The halting/stop and FSM obligations are landed and green on master. The request's CLI-reachability half is tracked at does/the/pipeline/and/fsm/reach/the/compiled/output.md (plan item 4); the reset-binding mapping remains the open half of section 2.
