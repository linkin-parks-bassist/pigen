---
status: green
revised_at: "2026-11-10T11:37:28+11:00"
---

pigen-fork runs a structured-only compiler. Done and verified end-to-end: declarations, abstract inputs, static/buffered transfers with real primitives, clocked processes including static updates, module emission, inline pipelines (both stage forms) with structured pipeline_test elasticity, FSMs, transfer-and-branch, `accepts`/`valid`/`ready`/`peek`, validity actions with static-signal diagnostics, grouped mutually exclusive storage producers, multi-module sources, retained ordinary-SV `assign`/instances/non-clocked `always` items, standard child instantiations, semantic instance/port metadata, inline fabric endpoint wiring plus standalone fabric block synthesis (routers, skid hops, path metadata, deterministic SVG), priority/router arbitration, typed and `integer`/`int` parameters, constant operator evaluation, aligned/misaligned concatenation destinations, signedness reinterpretation, `$signed`/`$unsigned`, mixed-integral conditionals, and aliased arithmetic widening. Ten examples are migrated. `make test` passes through pipeline-test; the smoke suite now reaches port_bram.

Remaining work, in order:
1. Finish the smoke corpus. Next failure: `port_bram.pigen` writes `mem[write_address] <= data_in` but the projection discards its index, so lowering updates the whole memory. Preserve projection index/select paths in lvalue lowering (RTL update destination must carry the element/select path), then continue through the remaining smoke fixtures.
2. Aggregate/function/struct forms (including function calls and saturation helpers), then migrate `biquad_bank` and `df1_biquad_bandpass`.
3. Complete remaining ordinary-SV expression coverage (aliases, macro forms, `_` concatenation placeholder).

Open questions for David: pipeline yield control surface; retention of fabric manifest/SVG; struct-like child-instantiation grammar (is/struct/like/child/instantiation/specified.md).

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Lowering consumes validated identities and backend-neutral records; stages diagnose and stop without rollback.
