---
status: green
revised_at: "2026-11-11T15:42:24+11:00"
---

Remaining work, next first:

1. Implement parent-wide `instance.port` endpoints, implicit parent nets, partial positional instance connections, endpoint-valued named connections and anonymous fabrics from the concrete child-connection requirement in what/is/the/spec.md. Add behavioral tests for each form and their combination.
2. Complete mixed-case selector evaluation, qualified cases and specialization convergence. Wildcard cases now use native SV combinational decoders, verified against reference casez/casex behavior. Add failing behavioral witnesses before each extension. Common width/signedness, nested ordinary cases, ternary labels and optional default colons now have passing simulations; full verification passes.
3. Complete sequential lowering for case guards, then loops and impure function/task side effects, then nonblocking write-conflict reconstruction. Keep unsafe forms diagnosed explicitly.
4. Continue the requirement-by-requirement acceptance audit against what/is/the/spec.md, including the compatibility contract. Do not shrink requirements to supported syntax. Capabilities and current gaps belong in what/is/broken.md; verification procedure belongs in how/to/run/repository/verification.md.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
