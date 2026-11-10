---
status: green
revised_at: "2026-11-10T11:23:08+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes, module emission, inline pipelines (both stage forms) with structured pipeline_test elasticity, FSMs, transfer-and-branch, `accepts`/`valid`/`ready`/`peek`, validity actions with static-signal diagnostics, grouped mutually exclusive storage producers, multi-module sources, verbatim ordinary-SV items and standard child instantiations, semantic instance/port metadata, inline fabric endpoint wiring plus standalone top-level fabric block synthesis (generated modules, routers, skid hops, path metadata and deterministic SVG), priority and router arbitration for routed sources, typed and `integer`/`int` parameters, constant division/modulo and full constant operator evaluation, aligned/misaligned concatenation destinations, signedness reinterpretation, `$signed`/`$unsigned`, mixed-integral conditionals, and aliased arithmetic widening. Ten examples are migrated. `make test` now passes through pipeline-test and stops in the legacy `tests/smoke.sh` fixture suite.

Remaining work, in order:
1. Modernize the `tests/smoke.sh` fixtures (legacy transfer-first declarations) to data-first syntax and rebaseline assertions to the structured emitter, so `make test` completes; take the failures in script order and keep each fixture's behavioural intent.
2. Aggregate/function/struct forms (including function calls and saturation helpers), then migrate `biquad_bank` and `df1_biquad_bandpass`.
3. Complete remaining ordinary-SV expression coverage (aliases and macro forms).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
