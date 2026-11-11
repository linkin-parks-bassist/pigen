---
status: green
revised_at: "2026-11-11T18:47:47+11:00"
---

Remaining work, next first:

1. Implement when (destination <= source) as transfer-fire event control. Restore ordinary <= comparisons in if, preserve atomic event-body firing, migrate legacy transfer-if syntax/tests and replace accepts workarounds in showcases. This surface is specified, not implemented; see how/to/write/fsm/states.md.

2. Complete nested selection mappings, partial-region dynamic selections, selections spanning producers and multidimensional mappings. Complete multiword power/constant evaluation, standalone negative exact-type RTL lowering, untyped negative bitwise typing and unresolved bounds. Audit direct local/ordinary-SV writers against aliased endpoint destinations and cross-instance clocks. Resolve explicit-valid gate versus replacement for computed automatic inputs, then implement it; conflicting read-only valid currently diagnoses. Complete other expressions and arrays/generate scopes. See how/to/connect/child/ports.md, what/is/elastic/rtl/task/five.md and what/is/an/inline/fabric.md.
3. Complete mixed-case selector evaluation and qualified cases. Audit specialization diagnostic provenance. Fixed-point handling, resource-limit errors and resolved-hierarchy cycle checks are implemented (how/does/specialization/reach/a/fixed/point.md); generate-scoped hierarchy remains under item 2. Wildcard cases use native SV combinational decoders, verified against casez/casex references. Add failing behavioral witnesses before each extension.
4. Complete sequential lowering for case guards, then loops and impure function/task side effects, then nonblocking write-conflict reconstruction. Keep unsafe forms diagnosed explicitly.
5. Audit remaining authored examples for numerical Pigen types, always processes and implicit transfer guards; keep compatibility witnesses distinct from showcases (where/are/behavioral/examples.md). Continue requirement-by-requirement acceptance against what/is/the/spec.md and the compatibility contract. Do not shrink requirements to supported syntax. Capabilities and gaps belong in what/is/broken.md; verification procedure belongs in how/to/run/repository/verification.md.

Implement optional FSM names and ordinary unconditional level-zero statements. Reset becomes ordinary if(reset) goto initialise; initialise is an ordinary state. The earlier reset-intrinsic proposal is superseded. Implement first-executed-goto termination of the entire FSM invocation: suppress all later FSM actions, preserve earlier actions and restart at the top next edge with the destination state. Verify nested branches, level-zero reset dominance, transfers before/after goto and independent processes. Startup without reset remains open (how/to/write/fsm/states.md).

Open questions for David: explicit-valid override semantics (how/to/connect/child/ports.md); pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
