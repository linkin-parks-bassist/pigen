---
status: green
revised_at: "2026-11-10T05:32:59+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes, module emission, inline pipelines (both stage forms), FSMs with state register/guards/gotos, `if (destination <= source)` transfer-and-branch, and the `accepts`/`valid`/`ready`/`peek` intrinsics. Simulations pass for the buf chain, an arithmetic pipeline, an FSM counter, the README sender FSM and an accepts()-based sender.

Remaining work, in order:
1. Add child-module instance syntax and semantic resolution (module/port binding, width/signedness checks, named connections) and emit named instances with definitions.
2. Build the inline parent-owned fabric on those instances: exclusive `>` and routed `->`/`-->` links, balanced three-port routers, buffered ready breaks, arbitration, and SVG from the same topology. Delete the quarantined prototype fabric once covered.
3. Implement the validity actions `validate`, `invalidate` and `flush`.
4. Complete ordinary-SV expression coverage (aliases, conditionals, remaining conversions).
5. Rebaseline the nine rtl-lower and two sv-emit sections to the new endpoint/fire/instance contract.
6. Migrate examples/ to data-first syntax.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
