---
status: green
revised_at: "2026-11-10T11:13:52+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes, module emission, inline pipelines (both stage forms), FSMs, transfer-and-branch, `accepts`/`valid`/`ready`/`peek`, validity actions, grouped mutually exclusive storage producers, multi-module sources, verbatim ordinary-SV items and standard child instantiations, semantic instance/port metadata, inline fabric endpoint wiring with direct links and skid-buffered routed links, priority merge for several routed sources per destination, typed and `integer`/`int` parameters folded to typed literals, constant division/modulo and full constant operator evaluation, aligned and misaligned concatenation destinations, signedness-only assignment reinterpretation, `$signed`/`$unsigned` casts, conditionals with mixed integral branches, and aliased arithmetic widening. Ten examples are migrated (including text). `make test` now advances through the syntax-model and integer suites to the standalone fabric gate; resolve/expression suites, rtl-lower 24/24 and sv-emit 4/4 are green.

Remaining work, in order:
1. Implement standalone top-level `fabric` network declarations so `fabric-test` passes: generated module/ports, router networks, buffered hops, path metadata and SVG. Standalone fabric fixtures currently resolve to zero semantic modules, so naming fails.
2. Fair round-robin grants for merged routed sources.
3. Aggregate/function/struct forms (including function calls and saturation helpers), then migrate `biquad_bank` and `df1_biquad_bandpass`.
4. Complete remaining ordinary-SV expression coverage (aliases and macro forms).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
