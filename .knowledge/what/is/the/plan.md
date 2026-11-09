---
status: green
revised_at: "2026-11-10T08:27:06+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes, module emission, inline pipelines (both stage forms), FSMs with state register/guards/gotos, `if (destination <= source)` transfer-and-branch, the `accepts`/`valid`/`ready`/`peek` intrinsics, multi-module sources, and standard child instantiations parsed structurally (named/positional connections, optional parameters) while emitted verbatim. Simulations pass for the buf chain, an arithmetic pipeline, an FSM counter, the README sender FSM, an accepts()-based sender and a standard child instance.

Remaining work, in order:
1. Resolve parsed instance nodes into semantic instance/port metadata: child module, port signal, width/signedness compatibility and connected parent nets, without changing emitted standard SV.
2. Build the inline parent-owned fabric on those records: exclusive `>` and routed `->`/`-->` links, balanced three-port routers, buffered ready breaks, arbitration, and SVG from one topology.
3. Implement the validity actions `validate`, `invalidate` and `flush`.
4. Rebaseline semantic-test's alias conversion assertion and the nine rtl-lower/two sv-emit stale sections to the current endpoint/fire/instance contract.
5. Complete ordinary-SV expression coverage (aliases, conditionals, remaining conversions).
6. Migrate examples/ to data-first syntax.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
