---
status: green
revised_at: "2026-11-10T13:05:21+11:00"
---

pigen-fork runs a structured-only compiler. `make test` and `make verify` are both fully green, including both biquad examples, all waveform gates, fabric synthesis and the smoke corpus. Done and verified: declarations, abstract inputs, static/buffered transfers with primitives, clocked processes with static/indexed destinations and unpacked memory shapes, combined exclusive readies, data-first `buf` pipeline fields including typedef aliases, pipelines with structured diagnostics, FSMs, transfer-and-branch, accessors, validity actions, concatenation and scalar-RHS co-slicing, nested concatenation destinations, packed struct typedefs with member reads and writes, inline and standalone fabric synthesis, typed/integer parameters, constant operators, signedness reinterpretation, `$signed`/`$unsigned`, mixed conditionals, aliased widening and signedness, sized literal concat members, indexed part-selects, one-bit primitive ties, and edge/destination diagnostics.

Remaining known spec gaps to audit and close:
1. Pipeline reset bindings (`pipeline_reset(name)` is not accepted by the structured parser; generated buffers currently rely on the domain reset).
2. Function declarations/calls and saturation helpers as accepted ordinary-SV forms.
3. `_` discard placeholder in concatenation destinations.
4. Macro concatenation/stringification/required-argument forms beyond object-like defines.
5. Run a fresh requirement-by-requirement acceptance audit against `what/is/the/spec.md`, adding failing witnesses before implementation.

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
