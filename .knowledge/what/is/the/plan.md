---
status: green
revised_at: "2026-11-10T12:05:08+11:00"
---

pigen-fork runs a structured-only compiler. `make test` is fully green. `make verify` now passes every gate except the aggregate-dependent ones: signal-syntax (struct member reads), biquad-bank and df1-biquad-bandpass. Done and verified: declarations, abstract inputs, static/buffered transfers with primitives, clocked processes with static/indexed destinations and unpacked memory shapes, combined exclusive readies, module emission, pipelines with structured diagnostics, FSMs, transfer-and-branch, accessors, validity actions (same-cycle transfer overrides invalidate), concatenation and scalar-RHS co-slicing, fabric synthesis with routers/SVG, parameters, constant operators, signedness reinterpretation, `$signed`/`$unsigned`, mixed conditionals, aliased widening, and edge diagnostics. All waveform gates, clear/fsm/vertical-slice, coslice, slicing, validate and signed-widen pass.

Remaining work, in order:
1. Aggregate/struct forms: implement packed struct member reads/writes (next failure `signal-syntax-test`, `source_struct.tag`), then function calls and saturation helpers required by the biquad examples.
2. Migrate `examples/biquad_bank.pigen` and `examples/df1_biquad_bandpass.pigen` to data-first structured form and take `biquad-bank-test`/`biquad-waveform` to green.
3. Complete remaining ordinary-SV expression coverage (macro forms, `_` concatenation placeholder).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
