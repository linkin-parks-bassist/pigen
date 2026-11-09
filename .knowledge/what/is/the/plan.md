---
status: green
revised_at: "2026-11-10T10:17:48+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes, module emission, inline pipelines (both stage forms), FSMs, transfer-and-branch, `accepts`/`valid`/`ready`/`peek`, validity actions, grouped mutually exclusive storage producers, multi-module sources, verbatim ordinary-SV items and standard child instantiations, semantic instance/port metadata, inline fabric endpoint wiring with direct links and skid-buffered routed links, aligned concatenation destinations with per-child conversion, and ten migrated examples (including text). All structured owner tests are green, rtl-lower 24/24.

Remaining work, in order:
1. Complete fabric routing: balanced three-port router networks, buffered hops, round-robin arbitration for multiple routed sources per destination, and SVG from the same topology.
2. Support typed parameters and mixed pigen-integer × ordinary-logic arithmetic; generalize concatenation (`_`, misaligned) and aggregate/function/struct forms; migrate `biquad_bank` and `df1_biquad_bandpass`.
3. Complete ordinary-SV expression coverage (aliases, conditionals, remaining conversions).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
