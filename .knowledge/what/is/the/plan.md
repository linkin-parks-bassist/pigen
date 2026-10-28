---
status: green
revised_at: "2026-10-29T08:00:34+11:00"
---

Completed on master: the quarantined elastic RTL vertical slice (Tasks 8-12; Task 7 ready-cycle validation deferred to Pigen 1.0), the CLI wiring (src/pigen.c main() calls pigen_compile_source, silent success / loud failure, what/is/the/command/line/contract.md), the retained core (resolved module/declarations/clocked process/direct atomic transfer route with the whole-unit ready graph, plus projection/concat/co-slice, validity/domain/ownership and stall/throughput laws, each with a green discriminating manifest), the Verilator-lint scope (pinned by tests/lint_fatal_pin.sh), the co-slice X-propagation and typedef-width defects (fixed), the FSM state-body fail-loud diagnostic, the FSM parent-module lowering (s2/s5, what/is/the/fsm/parent/module/lowering.md), the (i1) child-instance structured syntax node (PIGEN_SYNTAX_INSTANCE / PIGEN_SYNTAX_INSTANCE_CONNECTION parse, fail-loud at the offending token, no fallback) and the (i2) child-instance identity owner (surface-independent token-text resolution, fail-loud sentinel form).

Remaining approved work, in order:

1. The remaining subsystem build gates at what/are/the/remaining/architecture/migration/gates.md. Each subsystem is specified from what/is/the/spec.md and built on the structured path; the prototype is a hint only. No prototype-parity acceptance; no validator bridge, fallback, second dialect or per-file choice.

2. Pipeline: steps (a) identifier, (b) stage laws, (c) parent-module lowering are COMPLETE on master (a80e265; what/is/the/pipeline/parent/module/lowering.md); a failed (c) call reports at the original span and returns -1 with no rollback (how/should/compiler/builders/fail.md).

3. FSM: (s1) syntax COMPLETE and fail-loud including the state-body ordinary-statement check; (s2) identity COMPLETE with a real, discriminatively pinned success path; (s4) record-contract manifest green at set level (reset value, state-active and per-goto guard gating are record-level unobservable); (s5) implementation COMPLETE, carrying the three behavioral obligations structurally as (c3) carries its guarded handshake. The FSM lowers to one synchronous controller (what/is/an/fsm.md). Child-instance: (i1)/(i2) COMPLETE; (i3) parent-module lowering remains, then fabric.

4. CLI integration for pipelines and FSMs (first gap of item 1 for both): on master the CLI does not compile them - a pipeline statement fails loudly, a module-level FSM is silently dropped into an empty module (does/the/pipeline/and/fsm/reach/the/compiled/output.md). Next bounded step: production lowering of the pipeline statement through the complete pigen_pipeline_lower owner from the clocked-process path, with guarded enable/reset, one-item-per-cycle and yield obligations witnessed in the emitted SV; then the FSM through pigen_fsm_lower (silent drop becomes the lowered controller or a fail-loud diagnostic); then per-subsystem corpus migration to the data-first dialect for end-to-end simulation (pipeline-test/fsm-test), and tightened test contracts enforcing the required computation and halt/stop semantics.

5. Deferred to Pigen 1.0 with Task 7: the three route contract-level negative witnesses (cycle/self-loop rejection, sizing/signedness/aggregate-law violation, non-isolated foreign-module transfer rejection), none constructible through the public owner APIs. The concatenated/co-slice member-order law is pinned per path by emitted SV and golden witnesses (does/the/concatenation/data/type/keying/keep/member/order.md).

6. Open questions where the spec is silent on something only the prototype does (David's to decide; not inherited): (a) pipeline "yield" - is exposing the final outgoing value the complete pipeline control surface; (b) fabric manifest and SVG output - keep or dropped; (c) prototype-only examples, tests and APIs - preserved or retired when the textual machinery is deleted.

Lowering consumes validated identities and backend-neutral realization properties. Compiler stages fail loudly and stop, with no rollback requirement (how/should/compiler/builders/fail.md). Ordinary branches and worktrees may isolate narrow checked work; neither linear history nor direct-to-master work is required.
