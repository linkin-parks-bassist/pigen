---
status: green
revised_at: "2026-11-10T11:49:45+11:00"
---

pigen-fork runs a structured-only compiler. `make test` now completes end to end. Done and verified: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes with static updates and indexed/selected destinations, unpacked memory shapes, combined exclusive source-ready equations, module emission, inline pipelines both stage forms, FSMs, transfer-and-branch, accessors, validity actions with static diagnostics, grouped storage producers, multi-module sources, retained ordinary-SV items, child instantiations, inline fabric plus standalone fabric synthesis (routers, skid hops, path metadata, SVG), typed/integer parameters, constant operator evaluation, concatenation destinations aligned and misaligned, signedness reinterpretation, `$signed`/`$unsigned`, mixed-integral conditionals, aliased arithmetic widening, and multi-edge always_ff rejection. resolve/expression/semantic/rtl/sv-emit/compile/fabric/core-language/pipeline suites and the smoke corpus pass.

Remaining work, in order:
1. Run `make verify` and take its failures in order: migrate the remaining legacy example/waveform fixtures (`biquad_bank`, `df1_biquad_bandpass`, and any stale smoke-era targets) and rebaseline only where the structured emitter intentionally differs.
2. Aggregate/function/struct forms (function calls and saturation helpers) required by the two biquad examples.
3. Complete remaining ordinary-SV expression coverage (aliases, macro forms, `_` concatenation placeholder).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
