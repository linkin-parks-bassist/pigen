---
status: green
revised_at: "2026-11-10T12:12:02+11:00"
---

pigen-fork runs a structured-only compiler. `make test` is fully green. `make verify` passes every gate except the aggregate-dependent ones: signal-syntax (packed struct typedefs and member reads), biquad-bank and df1-biquad-bandpass. Done and verified: declarations, abstract inputs, static/buffered transfers with primitives, clocked processes with static/indexed destinations and unpacked memory shapes, combined exclusive readies, module emission, pipelines with structured diagnostics, FSMs, transfer-and-branch, accessors, validity actions (same-cycle transfer overrides invalidate), concatenation and scalar-RHS co-slicing, inline and standalone fabric synthesis with routers/SVG, parameters, constant operators, signedness reinterpretation, `$signed`/`$unsigned`, mixed conditionals, aliased widening, sized one-bit primitive ties, and edge diagnostics. All waveform gates, clear/fsm/vertical-slice, coslice, slicing, validate and signed-widen pass.

Remaining work, in order:
1. Add packed-struct typedef support: parse `typedef struct packed { ... } name;` into a packed type with a member map, then resolve member reads (`source_struct.tag`) as typed selects. `signal-syntax-test` is the acceptance gate.
2. Add function calls and saturation helpers needed by the examples, then migrate `examples/biquad_bank.pigen` and `examples/df1_biquad_bandpass.pigen` to data-first structured form and take `biquad-bank-test`/`biquad-waveform` green.
3. Complete remaining ordinary-SV expression coverage (macro forms, `_` concatenation placeholder).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
