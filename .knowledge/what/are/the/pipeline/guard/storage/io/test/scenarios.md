---
status: green
revised_at: "2026-11-03T02:01:01+11:00"
---

## Scope

One test obligation from the pipeline guard law (step (c), the parent-module lowering): **when the complete enclosing procedural guard is false, the pipeline holds every stage's storage and disables both the input and output handshake**, so no item is accepted or emitted that cycle. This list maps the *storage-hold* and *I/O-handshake* effects of a false guard at the parent-module lowering seam (`tests/pipeline_lower_test.c`, target `pipeline-lower-test`, one `SECTION` plus one `tests/contracts.json` entry per scenario).

It does NOT re-map the ready equation's *value* — already mapped and green in `what/are/the/pipeline/halt/stop/test/scenarios.md`, whose six accepted sections (named by exact `SECTION` identifier in "Current coverage") cover the ready value carrying the complete guard, not being the constant-1 stand-in, staying constant-1 when unguarded, the compound guard, and the rendered ready. This list references those six by exact name and builds on the same guard fixture and seam without re-asserting any ready-value line.

## Spec and contract sources

- `what/is/the/spec.md`: an enclosing procedural guard enables the pipeline and, when false, holds all storage and disables the input and output handshakes so no item is accepted or emitted that cycle; every ready/valid equation is gated by that guard.
- `how/to/reset/and/enable/a/pipeline.md` step (c): the false guard holds every stage's storage and disables both handshakes; the ready/valid equations are ALL gated by the guard; a phase-local stand-in gating only the ready value is not an acceptable narrowing.
- `what/is/the/pipeline/parent/module/lowering.md` step (c): per-stage next-value equations; the parent-module RTL model the guard must gate.
- Disposition owner `how/is/the/pipeline/guard/law/enforced/by/the/published/model.md`: the guard law is enforced by wiring the guard into the storage update AND I/O handshake; the current gap is that the guard gates only the ready value.
- Seam: `tests/pipeline_lower_test.c` (target `pipeline-lower-test`, one `tests/contracts.json` entry per `SECTION`); owner `include/pigen/pipeline_lower.h` / `src/pipeline_lower.c` (5-arg `pigen_pipeline_lower`); RTL record model `include/pigen/rtl.h` (`pigen_rtl_equation` origin/destination/value; `pigen_rtl_instance` with a `connections` record range; kinds `PIGEN_RTL_EXPR_OBJECT/INTEGER/BINARY/CONDITIONAL`); terminal rendering `pigen_sv_render_expression` (include/pigen/sv_emit.h).

## Current coverage

`pipeline-lower-test` already pins the ready *value* — the six accepted sections `ready-guarded-by-en`, `ready-not-constant-one`, `unguarded-ready-constant-one`, `compound-guard-published`, `guarded-storage-holds` and `ready-renders-guard-sv` — and, for the unguarded pipeline, the storage/yield/member-order shape (`buf-storage` pins only `instance_count`; `yield-write` pins only the yield destination's provenance; neither pins a guard port or a guarded yield) and the per-stage next-value computation value (the six stage-computation sections pin the structural expression; the constant-0 stand-in is replaced). Not yet pinned: no per-stage next-value equation is a guard-gated hold mux (scenario line 1 `guard-io-1-next-value-holds` is authored and accepted as its discriminating red section); the stage-boundary buf instance is added with a `connections` count of 0 (no guard/valid port); the yield write is unconditional; the ready equation is gated but its guard expression is a dangling internal 1-bit object (its *value* is asserted by the ready-value sections). This list fills the storage-hold and I/O-handshake side; after it, the implementation must wire the guard into the storage update and I/O handshake (the disposition owner's change), the later owner that depends on this packet.

## Test scenarios

One line = one later test-author scenario, its own `SECTION` in `tests/pipeline_lower_test.c` with a `tests/contracts.json` entry, built on the established seam helpers (the guarded-fixture variant that locates the pipeline under its `PIGEN_SYNTAX_IF_STATEMENT` parent and exposes the if condition's expression, owned by the accepted ready-value list and reused here).

1. Per-stage next value holds on a false guard: a legal 2-stage pipeline nested under `if (en)` (1-bit input `en`, stage 1 `a <= x`, stage 2 `b <= a`, `yield b`) lowers to rc == 0 and publishes each stage's next-value equation with a value that is a mux (PIGEN_RTL_EXPR_CONDITIONAL) whose condition is, or contains in its reachable subtree, a PIGEN_RTL_EXPR_OBJECT whose origin span spells exactly `en` — the storage holds (keeps its previous value) while the guard is false, rather than the current constant-0 stand-in.
2. Hold mux keeps the stored value when the guard is false: the same guarded pipeline lowers to rc == 0 and each per-stage next-value equation's value is a PIGEN_RTL_EXPR_CONDITIONAL whose when-false child is a PIGEN_RTL_EXPR_OBJECT (the stage's stored value) and whose when-true child is the stage's computed next value — the false branch holds, the true branch advances (the computed-next-value child is the value published by the accepted stage-computation / stand-in-replacement packet, plan item 1).
3. Stage-boundary buf storage is published with a guard port: the guarded pipeline lowers to rc == 0 and publishes its stage-boundary buf storage (the N-1 = 1 one-entry primitive `pigen_rtl_instance`) with a non-empty `connections` range whose connection objects include a guard connection whose origin span spells `en` — a guard port, not the current connectionless instance.
4. Storage guard holds the buf (false-guard hold): the guarded pipeline lowers to rc == 0 and the stage-boundary buf instance's guard connection is gated by `en` such that a false guard holds the stored value (the published storage update keeps the previous value under `!en`) — the storage-hold side of the law, independent of the ready value.
5. Input handshake disabled on a false guard: the guarded pipeline lowers to rc == 0, its ready equation is gated by `en` (its value or reachable subtree is a PIGEN_RTL_EXPR_OBJECT spelling `en` — the input-handshake *effect* here, not the ready-value law) AND each stage's storage next value holds (scenario 1's hold mux present) — the input handshake is disabled and all storage holds together when the guard is false.
6. Output handshake disabled on a false guard: the guarded pipeline lowers to rc == 0 and the yield write to the outgoing packet (the final-stage write to the `yield` target) is gated by the guard — its published equation/update value is gated by, or contains in its reachable subtree, a PIGEN_RTL_EXPR_OBJECT whose origin span spells `en` — no item is emitted while the guard is false.
7. Unguarded pipeline keeps ungated storage and I/O: a legal 2-stage pipeline directly in the procedural block (no enclosing if statement, `a <= x`, `b <= a`, `yield b`) lowers to rc == 0 and its stage-boundary buf storage has no `en`-gated guard connection and its yield write is NOT gated (no `en`-spanning object in its reachable subtree; the unguarded yield stays a plain write) — the guard replacement does not over-gate the unguarded pipeline's storage or either I/O control.
8. Storage-hold and I/O-handshake render the guard through the terminal SV: the guarded pipeline lowers to rc == 0 and the per-stage next-value equation's value (the hold mux) and the yield write's value each render through `pigen_sv_render_expression` (after `pigen_rtl_assign_names`, PIGEN_RTL_NAME_SOURCE) as text containing the spelled guard signal `en` — the emitted storage update and output handshake are gated by the guard's spelling, not the stand-in.

## Minimal fixture prerequisites

- The guarded-fixture variant (`pl_parse_guarded` file-local helper, owned by the accepted ready-value list) is reused unchanged.
- NEW file-local helper: locate a per-stage next-value equation (the stage-boundary `pigen_rtl_equation` whose destination is the stage's outgoing-packet object) and expose its value's kind and reachable subtree — scenarios 1, 2, 5, 8.
- NEW file-local helper: locate the stage-boundary buf storage `pigen_rtl_instance` among `pigen_rtl_model.instances` and expose its `connections` record range and the connection objects' origin spans — scenarios 3, 4, 7.
- NEW file-local helper: locate the yield write to the outgoing packet (the final-stage write to the `yield` target object) and expose its published equation/update value and reachable subtree — scenarios 6, 7, 8.
- `pigen_sv_render_expression` + `pigen_rtl_assign_names` (PIGEN_RTL_NAME_SOURCE) cover scenario 8's rendered-mux and rendered-yield checks.
- No production-source change is part of any scenario: the authors register the discriminating sections; the guard-wiring implementation is the later owner that depends on this packet.

## Stand-in replacement (plan item 1) dependency

Scenario 2's when-true child is the stage's computed next value, published by the landed stand-in replacement (the per-stage next value is now the stage's structural expression over the immutable incoming fields, `what/are/the/pipeline/stage/computation/test/scenarios.md`). The accepted scenario-2 assertion pins the mux shape and the when-false stored-value object; the when-true child is the computed value that replacement publishes. The guard-wiring implementation lands after plan item 1, which is done.

## Exclusions (other scoped assignments)

The ready equation's *value* is owned by the six accepted sections named in "Current coverage" and is NOT re-mapped here. Reset binding, FSM behavior/stop, backpressure, throughput and CLI integration belong to the broader test-contract frontier; this list pins only the storage-hold and I/O-handshake effects of the guard at the lowering seam.
