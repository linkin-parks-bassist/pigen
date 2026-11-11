---
status: green
revised_at: "2026-11-11T16:29:14+11:00"
---

Remaining work, next first:

1. Complete explicit control overrides on computed child inputs, other expressions, input aliases, arrays/generate scopes and non-symbol controls. Audit broader aliases, retained ordinary-SV drivers and cross-instance clock compatibility. Add aliased-destination and repeated-source behavioral witnesses before extending fabric ownership/grouping. See how/to/connect/child/ports.md and what/is/an/inline/fabric.md.
2. Complete mixed-case selector evaluation and qualified cases. Audit specialization diagnostic provenance. Fixed-point handling, resource-limit errors and resolved-hierarchy cycle checks are implemented (how/does/specialization/reach/a/fixed/point.md); generate-scoped hierarchy remains under item 1. Wildcard cases use native SV combinational decoders, verified against casez/casex references. Add failing behavioral witnesses before each extension.
3. Complete sequential lowering for case guards, then loops and impure function/task side effects, then nonblocking write-conflict reconstruction. Keep unsafe forms diagnosed explicitly.
4. Continue requirement-by-requirement acceptance against what/is/the/spec.md and the compatibility contract. Do not shrink requirements to supported syntax. Capabilities and gaps belong in what/is/broken.md; verification procedure belongs in how/to/run/repository/verification.md.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
