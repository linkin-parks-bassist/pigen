---
status: green
revised_at: "2026-11-10T11:59:51+11:00"
---

pigen-fork runs a structured-only compiler. `make test` completes end to end, and `make verify` now passes every gate through pipeline-scope and pipeline-syntax. Done and verified: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes with static updates and indexed/selected destinations, unpacked memory shapes, combined exclusive source-ready equations, module emission, inline pipelines (including pipeline-stage diagnostics), FSMs, transfer-and-branch, accessors, validity actions, grouped storage producers, multi-module sources, retained ordinary-SV items, child instantiations, inline and standalone fabric synthesis (routers, skid hops, path metadata, SVG), typed/integer parameters, constant operator evaluation, concatenation destinations, signedness reinterpretation, `$signed`/`$unsigned`, mixed-integral conditionals, aliased arithmetic widening, and always_ff edge rejection.

Remaining work, in order:
1. `make verify` now stops at `biquad-bank-test`: migrate `examples/biquad_bank.pigen` to data-first structured form (it uses transfer-first `buf sample_t sample_in` and legacy constructs), then take the remaining verify gates in order.
2. Migrate `df1_biquad_bandpass` and add the aggregate/function/struct support it needs (function calls, saturation helper).
3. Complete remaining ordinary-SV expression coverage (aliases, macro forms, `_` concatenation placeholder).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
