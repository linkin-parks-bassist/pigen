---
status: green
revised_at: "2026-11-11T15:36:28+11:00"
---

Remaining work, next first:

1. Complete mixed-case selector evaluation and common case width/signedness semantics; audit nested case variants and bounded specialization convergence. Add failing behavioral witnesses before each extension. Ordinary selector evaluation, concatenated labels and unbraced if/else mixed bodies now have passing simulations; full verification passes.
2. Complete sequential lowering for case guards, then loops and impure function/task side effects, then nonblocking write-conflict reconstruction. Keep unsafe forms diagnosed explicitly.
3. Continue the requirement-by-requirement acceptance audit against what/is/the/spec.md, including the compatibility contract. Do not shrink requirements to supported syntax. Capabilities and current gaps belong in what/is/broken.md; verification procedure belongs in how/to/run/repository/verification.md.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
