---
status: green
revised_at: "2026-10-07T07:57:02+11:00"
---

Stance: the quarantined textual prototype is unmaintainable and has no standing as a reference. Retained subsystems are specified from what/is/the/spec.md and their owning leaves, and built on the structured path through ordinary skeleton, test and implementation work. The prototype is consulted at most as a hint about which features exist. No subsystem must reproduce prototype behaviour, and no gate requires prototype-parity or before/after equivalence.

Stance: the fixed signal-model and shared-frontend work is complete (one signal arena/binding, descriptor laws and realization families, data-type owner algebra, intrinsic operations, data-first declarations, abstract inputs/FIFO arguments). Remaining frontend work is the parameter/type/aggregate/array/expression forms and macro concatenation/stringification/required arguments required by accepted constructs; unsupported dependencies diagnose original spans instead of inspecting opaque text.

Retained core: specify and build one resolved module/declarations/clocked process/direct atomic transfer route, semantic sizing/signedness/aggregate checks, whole-unit ready graph, and the current projection/concat/co-slice/validity/domain/ownership/stall/throughput laws, from the spec and its owning leaves.

Pipeline: common identities for pipeline/stage/field/incoming/outgoing/external/reset/yield; parent-module lowering through common incidence/RTL; guarded execution, private scopes/shadow errors, atomic external inputs/reset/repartition/one-item-per-cycle; both stage statement forms; data-first implicit/explicit-buf-only fields. A spec-driven pipeline integration design proves the stage field/law contract.

FSM: shared state/initial/transition/priority/guard/action identities and normal single/multi-statement state bodies, with common RTL control, from the spec.

Fabric: child-module instance/port/type resolution before analysis and surface-independent identities; inline parent-owned two-component endpoints replacing top-level fixed-width units; per-connection type compatibility and no PAYLOAD_W contract; blind endpoints, exclusive direct/many-to-one arbitration, balanced topology, relative routes, reachability, buffered ready breaks, manifests and SVG from one model.

Deletion: for each subsystem, delete the corresponding textual scanner/parser/emitter in the same change that its structured path covers the spec. Production switches only then; no validator bridge, fallback, second dialect or per-file choice.

Acceptance: every accepted specified construct has behavioral/diagnostic/backpressure/atomicity/throughput coverage, exact original-span diagnostics, clean make verify with warnings-as-errors, localized primitive evolution and deterministic SV/SVG. A rejection is a specified deliberate restriction or an accidental bug. Open questions where the spec is silent on something only the prototype does are in what/is/the/plan.md; they are not inherited. These are acceptance contracts, not a claim that all gates have passed.
