---
status: green
revised_at: "2026-10-30T17:29:00+11:00"
---

## Scope

The user-requested pipeline/FSM behavioral test contracts, decomposed into three scoped obligations: (1) pipeline stage computation, (2) pipeline halting/stop (guarded enable), (3) FSM behavior/stop. CLI reachability of pipeline/FSM statements is a separate integration concern (does/the/pipeline/and/fsm/reach/the/compiled/output.md). Each obligation below names its current test coverage and its remaining owner in the product plan.

## 1. Pipeline stage computation — owned, in progress

Contract: each stage's `<=` computes the outgoing packet as expressions over the immutable incoming fields and enclosing signals (how/do/pipeline/stage/reads/work.md). Current coverage: pipeline-lower-test pins only equation count (section 2, next-value-equations); src/pipeline_lower.c publishes a constant-0 stand-in as each stage's next-value, so a lowering that computes nothing passes. Owner: the six-scenario list at what/are/the/pipeline/stage/computation/test/scenarios.md, accepted by the independent review at what/is/the/review/of/the/pipeline/stage/computation/test/scenarios.md; its six single-scenario sections (pipeline-lower-test seam) are in authorship, followed by the stand-in replacement implementation (plan item 1).

## 2. Pipeline halting/stop — review accepted

Contract: the complete enclosing procedural guard enables the pipeline; false holds all storage and disables input/output handshakes; pipe_reset/pipeline_reset guard bindings directly reset every generated valid bit (how/to/reset/and/enable/a/pipeline.md). Current coverage: pipeline-lower-test section 5 (step-b-witnesses) pins only that the guarded ready/valid handshake equations are published; the guard in src/pipeline_lower.c is the constant 1, so a false enclosing guard is not enforced - the published ready equation is ready = 1. Owner: the six-scenario list at what/are/the/pipeline/halt/stop/test/scenarios.md, accepted by the independent review at what/is/the/review/of/the/pipeline/halt/stop/test/scenarios.md; its six single-scenario sections (pipeline-lower-test seam) are in authorship, followed by the guard-replacement implementation (plan item 8). Reset-binding coverage follows in its own scoped mapping.

## 3. FSM behavior/stop — scoped list published, independent review in flight

Contract: one synchronous controller reset to its header initial state; each goto changes state only under its explicit enclosing guard and never implicitly waits for a transfer; the state holds otherwise (what/is/an/fsm.md, does/goto/wait/for/a/transfer.md, how/to/write/fsm/states.md). Current coverage: fsm-lower-test's four positive anchors (state-register-reset, per-state-action-bodies, guard-gated-transitions, structural-exclusion) pin only the SET-LEVEL record contract; they do NOT pin the reset VALUE (the header initial state's encoding), state-active gating or per-goto guard gating (what/is/the/fsm/parent/module/lowering.md, step (s4)). Owner: the five-scenario list at the fsm-lower-test seam, published at what/are/the/fsm/behavior/stop/test/scenarios.md (gate expressions witnessed as published record structure - the record model carries no guard or enable field - with a file-local non-first initial-state witness for the reset value); next its independent map review, then the one-scenario sections (plan item 9).

## Continuation

Items 8 and 9 of what/is/the/plan.md retain the halting and FSM obligations in order. The original broad contract-tightening request decomposes into these three scoped obligations; its CLI-reachability half is tracked at does/the/pipeline/and/fsm/reach/the/compiled/output.md (plan item 5).
