---
status: green
revised_at: "2026-11-10T08:38:05+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes, module emission, inline pipelines (both stage forms), FSMs, transfer-and-branch, `accepts`/`valid`/`ready`/`peek`, multi-module sources, verbatim ordinary-SV items and standard child instantiations, semantic instance/port metadata, and inline fabric endpoint wiring. Simulations pass for the buf chain, an arithmetic pipeline, an FSM counter, the README sender FSM, an accepts()-based sender, a standard child instance and a two-leaf fabric routed link.

Remaining work, in order:
1. Complete fabric: replace combinational routed wiring with balanced three-port routers and buffered ready breaks, add round-robin arbitration for multiple sources per destination, and generate SVG from the same topology.
2. Implement the validity actions `validate`, `invalidate` and `flush`.
3. Rebaseline semantic-test's alias conversion assertion and the nine rtl-lower/two sv-emit stale sections.
4. Complete ordinary-SV expression coverage (aliases, conditionals, remaining conversions).
5. Migrate examples/ to data-first syntax.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
