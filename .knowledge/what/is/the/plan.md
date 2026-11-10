---
status: green
revised_at: "2026-11-10T12:29:10+11:00"
---

pigen-fork runs a structured-only compiler. `make test` is fully green. `make verify` passes every gate except the two biquad examples. Done and verified: declarations, abstract inputs, static/buffered transfers with primitives, clocked processes with static/indexed destinations and unpacked memory shapes, combined exclusive readies, module emission, pipelines with structured diagnostics, FSMs, transfer-and-branch, accessors, validity actions, concatenation and scalar-RHS co-slicing, packed struct typedefs with member reads, inline and standalone fabric synthesis with routers/SVG, parameters, constant operators, signedness reinterpretation, `$signed`/`$unsigned`, mixed conditionals, aliased widening, sized literal concat members, indexed part-selects, sized one-bit primitive ties, and edge/destination diagnostics.

Remaining work, in order:
1. Migrate `examples/biquad_bank.pigen` to data-first structured form (ports are still transfer-first; transfer blocks, macro-heavy arrays and pipeline-local wires remain) and take `biquad-bank-test` green.
2. Migrate `examples/df1_biquad_bandpass.pigen`, adding the function-call/saturation-helper support it needs, then take `biquad-waveform` green.
3. Complete remaining ordinary-SV expression coverage (macro forms, `_` concatenation placeholder).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
