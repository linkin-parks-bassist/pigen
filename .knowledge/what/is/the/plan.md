---
status: green
revised_at: "2026-10-27T10:45:49+11:00"
---

Completed on master: the quarantined elastic RTL vertical slice (Tasks 8-12; Task 7 ready-cycle validation deferred to Pigen 1.0), the CLI wiring (src/pigen.c main() calls pigen_compile_source, silent success / loud failure, what/is/the/command/line/contract.md), the retained core (resolved module/declarations/clocked process/direct atomic transfer route with the whole-unit ready graph, plus projection/concat/co-slice, validity/domain/ownership and stall/throughput laws, each with a green discriminating manifest), the Verilator-lint scope (no -Wno-fatal anywhere, pinned by tests/lint_fatal_pin.sh) and the co-slice X-propagation and typedef-width defects (fixed).

Remaining approved work, in order:

1. The remaining subsystem build gates at what/are/the/remaining/architecture/migration/gates.md. Each subsystem is specified from what/is/the/spec.md and built on the structured path; the prototype is a hint only about which features exist. No prototype-parity acceptance; no validator bridge, fallback, second dialect or per-file choice.

2. The pipeline subsystem is COMPLETE on master, decomposed at what/is/the/pipeline/parent/module/lowering.md: steps (a) the identifier, (b) the stage laws and (c) parent-module lowering all landed (a80e265); step (c) lowers one PIGEN_SYNTAX_PIPELINE statement through the common RTL model, a failed call reports at the original span and returns -1 with no rollback (how/should/compiler/builders/fail.md).

3. The FSM subsystem is the current frontier, decomposed at what/is/the/fsm/parent/module/lowering.md: step (s1) the structured FSM syntax node is COMPLETE; step (s2) the FSM identity owner (include/pigen/fsm_identity.h + src/fsm_identity.c; target fsm-identity-test) has its owner seam established as an interface-only skeleton (establish validates and returns -1, the is_invalid predicate is real), with the implementation and the preserved fsm-identity-test packet still pending; step (s3) the parent-module lowering skeleton (include/pigen/fsm_lower.h + src/fsm_lower.c) has its owner seam established on master as an interface-only skeleton (pigen_fsm_lower validates its arguments and returns -1 without publishing any RTL record, until (s5)); the remaining steps are (s4) the discriminating green manifest (tests/fsm_lower_test.c; target fsm-lower-test, authored and preserved on branch work/fsm-lower-skeleton) and (s5) the implementation to that manifest, each in its own bounded stage; after (s5), child-instance and fabric follow. The FSM lowers to one synchronous controller (state register reset to the header initial state, per-state action bodies through the shared services, guard-gated goto transitions with structural exclusion), per what/is/an/fsm.md.

4. Deferred to Pigen 1.0 with Task 7: the three route contract-level negative witnesses (cycle/self-loop rejection, sizing/signedness/aggregate-law violation, non-isolated foreign-module transfer rejection), none constructible through the public owner APIs. The concatenated/co-slice member-order law is unobservable through the public API and pinned per path by emitted SV and golden witnesses (does/the/concatenation/data/type/keying/keep/member/order.md).

5. Open questions where the spec is silent on something only the prototype does (David's to decide; not inherited): (a) pipeline "yield" - is exposing the final outgoing value the complete pipeline control surface; (b) fabric manifest and SVG output - keep or dropped; (c) prototype-only examples, tests and APIs - preserved or retired when the textual machinery is deleted.

Lowering consumes validated identities and backend-neutral realization properties. Compiler stages fail loudly: a stage reports the diagnostic and stops, with no rollback requirement (how/should/compiler/builders/fail.md). Ordinary branches and worktrees may isolate narrow checked work; neither linear history nor direct-to-master work is required.
