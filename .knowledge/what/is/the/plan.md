---
status: green
revised_at: "2026-11-10T09:59:48+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes, module emission, inline pipelines (both stage forms), FSMs, transfer-and-branch, `accepts`/`valid`/`ready`/`peek`, validity actions, multi-module sources, verbatim ordinary-SV items and standard child instantiations, semantic instance/port metadata, inline fabric endpoint wiring with direct links and skid-buffered routed links, aligned concatenation destinations with per-child conversion, one RTL process per semantic process, and nine migrated examples. semantic-test, compile-test, sv-emit-test, concat-dest-test pass; rtl-lower-test is 21/24.

Remaining work, in order:
1. Complete fabric routing: balanced three-port router networks, buffered hops, round-robin arbitration for multiple routed sources per destination, and SVG from the same topology.
2. Rebaseline the three stale t8 sections (fire-identity, guard, ownership) to update-value-RHS/fire-in-controls semantics.
3. Support typed parameters and mixed pigen-integer × ordinary-logic arithmetic; generalize concatenation (`_`, misaligned) and aggregate/function forms; migrate `text`, `biquad_bank`, `df1_biquad_bandpass`.
4. Complete ordinary-SV expression coverage (aliases, conditionals, remaining conversions).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
