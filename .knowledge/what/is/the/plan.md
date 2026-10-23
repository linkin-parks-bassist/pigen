---
status: green
revised_at: "2026-10-24T10:38:50+11:00"
checked_at: "2026-09-30T20:31:54+10:00"
---

Remaining approved migration work, in order:

1. Complete pipeline parent-module lowering through common incidence/RTL: the discriminating test manifest, implementation and landing. Preserve outgoing-packet member order (first yielded field is the MSB), atomic external consuming reads under the full guard, and at most one item per cycle. The recorded master seam is the five-argument `pigen_pipeline_lower(lowering, module, syntax, pipeline, expanded)`; the remaining bounded sequence is test correction, implementation, then landing. Focused syntax, pipeline field/output identities and stage-law results belong to their subsystem owners rather than remaining tasks here.
2. Build FSM, child-instance and fabric support on the structured path, in that order, with discriminating behavioral and diagnostic manifests. Follow `what/are/the/remaining/architecture/migration/gates.md`; specify behavior from the spec, with no prototype-parity requirement.
3. Complete accepted parameter/type/aggregate/array/expression forms and macro concatenation/stringification/required arguments as required by the retained constructs. Unsupported dependencies diagnose original spans.
4. Satisfy production acceptance and replace corresponding textual machinery in the same subsystem cutover. No validator bridge, fallback, second dialect or per-file choice is approved. No dated CLI cutover milestone is recorded.

Integration evidence is a blocker to the prior Task 11/12 completion claim: the inspected working checkout's compiler composer rejects semantic resolution's success return, compile tests expect failure, and vertical-slice-test runs the prototype CLI. See `does/the/structured/frontend/run/in/production.md`. These observations establish a checkout defect, not a comparison against the newer master commit. Reconcile the checkout and master composition/test evidence before relying on complete structured end-to-end acceptance; successful structured compilation and simulation remain unestablished by the inspected tests. Fixing this before pipeline work is a recommendation, not a user-approved reorder of the existing plan.

Task 7 ready-cycle validation and the three unconstructible contract-level negative route witnesses remain deferred to Pigen 1.0. Keep emitted SV/golden witnesses for concatenation/co-slice member order, which width-only data-type interning cannot establish.

Open product decisions for David: whether exposing the final outgoing value through pipeline yield is the complete control surface; whether fabric manifests and SVG output stay; and which prototype-only examples, tests and APIs are preserved or retired when textual machinery is deleted.

Lowering consumes validated identities and backend-neutral realization properties. Stages report diagnostics and stop without a rollback requirement. Ordinary branches/worktrees may isolate checked work; neither linear history nor direct-to-master work is required.
