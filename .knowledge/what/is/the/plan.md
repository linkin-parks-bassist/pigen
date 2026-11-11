---
status: green
revised_at: "2026-11-11T15:55:57+11:00"
---

Remaining work, next first:

1. Preserve explicit valid/ready control aliases when a connected child port is reused via instance.port: the current `.i(a.o)` path stalls when a.o uses separately named payload/control nets. Then complete general endpoint expressions, input aliases, arrays/generate scopes, control access, fabric ownership and unused-endpoint defaults. Forward sibling references, later module definitions and child/local consumer exclusivity are implemented and verified. See how/to/connect/child/ports.md.
2. Complete mixed-case selector evaluation, qualified cases and specialization convergence. Wildcard cases now use native SV combinational decoders, verified against reference casez/casex behavior. Add failing behavioral witnesses before each extension. Common width/signedness, nested ordinary cases, ternary labels and optional default colons now have passing simulations; full verification passes.
3. Complete sequential lowering for case guards, then loops and impure function/task side effects, then nonblocking write-conflict reconstruction. Keep unsafe forms diagnosed explicitly.
4. Continue the requirement-by-requirement acceptance audit against what/is/the/spec.md, including the compatibility contract. Do not shrink requirements to supported syntax. Capabilities and current gaps belong in what/is/broken.md; verification procedure belongs in how/to/run/repository/verification.md.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
