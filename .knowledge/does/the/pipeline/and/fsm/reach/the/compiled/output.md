---
status: green
revised_at: "2026-10-29T07:46:34+11:00"
---

On accepted master (418adb1), pipeline and FSM statements do not reach the CLI-compiled output.

- Pipeline: a `pipeline ... endpipeline` statement in a clocked process parses (PIGEN_SYNTAX_PIPELINE), but resolve_procedural_statement (src/resolve.c:641) accepts only nonblocking assignment, if statement and procedural block; a pipeline statement reports `invalid procedural statement` at its original span and the CLI exits nonzero with no output file.
- FSM: a module-level `fsm ... begin ... end` statement parses (PIGEN_SYNTAX_FSM) but has no handler in the production semantic/RTL path: the CLI exits 0 and emits an empty module - the FSM is silently dropped from the emitted SV.

Checked probes (data-first dialect, ./pigen on the 418adb1 build): a two-stage pipeline source -> RC=1, `invalid procedural statement` at the `pipeline` token; an idle/send FSM with a guarded goto -> RC=0, output module body empty.

Cause: the production compile path (src/compile.c -> pigen_lower_rtl_transfers / pigen_lower_rtl_module in src/rtl_lower.c) lowers declarations and direct transfers only. The complete, green-manifested lowering owners pigen_pipeline_lower (src/pipeline_lower.c, step (c), what/is/the/pipeline/parent/module/lowering.md) and pigen_fsm_lower (src/fsm_lower.c, (s5), what/is/the/fsm/parent/module/lowering.md) are not called from the production path; the quarantined textual fsm.c/pipeline.c are still in the production link line.

Consequence: the user-facing outcome - a .pigen source containing a pipeline or FSM compiles through the CLI to SV that implements the specified computation and halt/stop semantics, with test contracts enforcing it - is not met. The cli-wiring milestone covered the CLI shell itself (what/is/the/command/line/contract.md), not statement lowering through it. The corpus tests/pipeline.pigen and tests/fsm.pigen remain prototype-dialect and go red as per-subsystem migration debt (what/is/broken.md); the pipeline-test/fsm-test verify targets therefore also miss the structured dialect.

Remaining CLI integration, in product order: (1) production lowering of the pipeline statement through pigen_pipeline_lower from the clocked-process path, with its guarded enable/reset, one-item-per-cycle and yield obligations in the emitted SV; (2) production lowering of the FSM statement through pigen_fsm_lower, with the silent drop replaced by the lowered controller or a fail-loud diagnostic; (3) per-subsystem migration of the retained corpus to the data-first dialect so the CLI-compiled SV is simulated end-to-end (pipeline-test/fsm-test); (4) tightened test contracts enforcing required computation and halt/stop semantics at the CLI level. Behavioral contract owners: how/do/pipeline/stage/reads/work.md, how/to/reset/and/enable/a/pipeline.md, what/does/pipeline/yield/mean.md, how/to/write/fsm/states.md, does/goto/wait/for/a/transfer.md.
