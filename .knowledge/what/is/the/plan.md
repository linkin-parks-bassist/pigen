---
status: green
revised_at: "2026-11-10T11:42:43+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes including static updates and indexed/selected destination paths, module emission, inline pipelines (both stage forms), FSMs, transfer-and-branch, `accepts`/`valid`/`ready`/`peek`, validity actions with static-signal diagnostics, grouped mutually exclusive storage producers, multi-module sources, retained ordinary-SV `assign`/instances/non-clocked `always` items, standard child instantiations, semantic instance/port metadata, inline fabric endpoint wiring plus standalone fabric block synthesis (routers, skid hops, path metadata, deterministic SVG), priority/router arbitration, typed and `integer`/`int` parameters, constant operator evaluation, aligned/misaligned concatenation destinations, signedness reinterpretation, `$signed`/`$unsigned`, mixed-integral conditionals, aliased arithmetic widening, and rejection of multi-edge always_ff. Ten examples are migrated. `make test` passes through pipeline-test; the smoke suite now reaches port_bram and stops because an unpacked memory object loses its array shape when emitted.

Remaining work, in order:
1. Carry unpacked array shape into RTL objects and emit it (`logic [7:0] mem [0:15]`), so `port_bram`'s indexed reads/writes lint cleanly; then finish the remaining smoke fixtures and diagnostics.
2. Aggregate/function/struct forms (including function calls and saturation helpers), then migrate `biquad_bank` and `df1_biquad_bandpass`.
3. Complete remaining ordinary-SV expression coverage (aliases, macro forms, `_` concatenation placeholder).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
