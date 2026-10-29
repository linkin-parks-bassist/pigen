---
status: green
revised_at: "2026-10-30T08:36:14+11:00"
---

## Scope

One test obligation from the pipeline contract: **the complete enclosing procedural guard enables the pipeline, and a false guard holds all storage and disables both the input and output handshakes** — at the parent-module lowering seam this is the guard law: the pipeline's ready/valid equations are all gated by the complete enclosing procedural guard (include/pigen/pipeline_lower.h; how/to/reset/and/enable/a/pipeline.md). Reset-binding coverage (pipe_reset/pipeline_reset resetting the generated valid bits), FSM behavior/stop and CLI integration are separate scoped assignments; the stage-computation chain is a separate in-progress obligation (what/are/the/pipeline/stage/computation/test/scenarios.md).

## Spec and contract sources

- `what/is/the/spec.md`: "complete procedural guards enable handshakes, reset bindings reset generated valid bits".
- `how/to/reset/and/enable/a/pipeline.md`: the complete enclosing procedural guard enables the pipeline; false holds all storage and disables input/output handshakes.
- `include/pigen/pipeline_lower.h`: the guard law — the pipeline's ready/valid equations are all gated by the complete enclosing procedural guard; a false guard holds every stage's storage and disables both handshakes so no item is accepted or emitted that cycle.
- `what/is/the/pipeline/parent/module/lowering.md`: step (c) lowering; the guarded ready/valid handshake is one of the four published record families.
- `what/are/the/owners/of/the/pipeline/fsm/behavioral/test/obligations.md` (section 2): the exact current coverage and the constant-1 guard stand-in in src/pipeline_lower.c (the published ready equation is ready = 1).
- Seam: `tests/pipeline_lower_test.c` (target `pipeline-lower-test`, Makefile `T=<section>` entry per `SECTION`, one `tests/contracts.json` entry per section); owner `include/pigen/pipeline_lower.h` / `src/pipeline_lower.c` (5-arg `pigen_pipeline_lower`); file-local helpers `pl_parse`/`pl_unit`/`pl_port`/`pl_drive`; RTL record model `include/pigen/rtl.h` (`pigen_rtl_equation` destination/value; expression kinds `PIGEN_RTL_EXPR_OBJECT/BINARY/INTEGER/CONCATENATION`; `pigen_rtl_expr_children`); terminal rendering `pigen_sv_render_expression` (include/pigen/sv_emit.h) + `pigen_rtl_assign_names` (PIGEN_RTL_NAME_SOURCE).
- Guard syntax: a pipeline nested under `if (guard-expr)` parses to a PIGEN_SYNTAX_IF_STATEMENT whose `as.if_statement.condition` is the guard's pigen_syntax_expr; the pipeline node's `parent` is that if node. The step-(a) identity owner and step-(b) stage-law owner do not read the parent chain, so a guarded pipeline drives the existing seam unchanged.

## Current coverage

`pipeline-lower-test` section (5) `step-b-witnesses` pins only that the guarded ready/valid handshake equations are published (`instance_count >= 1`, `equation_count >= 1`) — it does NOT pin the guard's expression. `src/pipeline_lower.c` uses the constant 1 as the guard (`guard = one`, `ready = guard`), so the published ready equation is ready = 1 and a lowering that keeps guard = constant 1 passes the manifest. This list fills that gap; after it, the implementation must replace the constant-1 guard with the enclosing procedural guard's published expression.

## Test scenarios

One line = one later test-author scenario, registered as its own `SECTION` in `tests/pipeline_lower_test.c` with a `tests/contracts.json` entry, built on the established seam helpers plus the guarded-fixture extension below (lines 1–3 establish it; lines 4–6 reuse it).

1. Guarded pipeline fixture and ready gated by the guard signal: a legal 2-stage pipeline nested under `if (en)` inside the clocked process (1-bit module input `en`, stage 1 `a <= x`, stage 2 `b <= a`, `yield b`) lowers to rc == 0 and the pipeline's ready equation publishes a 1-bit value that is NOT an integer constant (PIGEN_RTL_EXPR_INTEGER) and that IS, or contains in its reachable expression subtree, a PIGEN_RTL_EXPR_OBJECT whose origin span spells exactly `en` in the parsed source.
2. Guarded pipeline ready is not the constant-1 stand-in: the same guarded pipeline lowers to rc == 0 and NO published 1-bit handshake equation (the pipeline's ready/valid equations) has an integer-constant value (PIGEN_RTL_EXPR_INTEGER) — the constant-1 stand-in the manifest must now discriminate; a lowering that keeps guard = constant 1 fails this section.
3. Unguarded pipeline keeps its constant-1 ready: a legal 2-stage pipeline directly in the procedural block (no enclosing if statement, stage 1 `a <= x`, stage 2 `b <= a`, `yield b`) lowers to rc == 0 and its ready equation publishes the constant-1 value (PIGEN_RTL_EXPR_INTEGER, 1-bit, value 1) — an unguarded pipeline is enabled, and its ready remains the constant 1.
4. Compound guard expression published: a legal 2-stage pipeline nested under `if (en && f)` (1-bit module inputs `en`, `f`) lowers to rc == 0 and the pipeline's ready equation value is a binary logical-AND expression (PIGEN_RTL_EXPR_BINARY) whose reachable object children include objects with origin spans spelling `en` and `f` — the complete enclosing guard, not a single atom and not a constant.
5. Guarded storage holds: a legal 2-stage pipeline nested under `if (en)` (1-bit input `en`) lowers to rc == 0, publishes its stage-boundary buf storages (the N-1 = 1 one-entry primitive instance) AND its ready equation gated by `en` — the false guard holds all storage while disabling the handshake, and a lowering that drops the storage or publishes an unguarded ready fails this section.
6. Guarded ready renders the guard signal through the terminal SV: the guarded pipeline of scenario 1 lowers to rc == 0 and the ready equation's value renders through `pigen_sv_render_expression` (after `pigen_rtl_assign_names`, PIGEN_RTL_NAME_SOURCE, for the reachable object origins) as text containing the spelled guard signal `en` and not the bare constant `1` — the emitted handshake is gated by the guard's spelling, not the stand-in.

## Minimal fixture prerequisites

- Guarded-pipeline fixture extension (lines 1–3 own it; lines 4–6 reuse it): the file-local helpers of tests/pipeline_lower_test.c locate the pipeline as the first DIRECT child of the procedural block (`pl_parse`: `block->first_child`, REQUIRE kind == PIGEN_SYNTAX_PIPELINE), so a guarded pipeline needs a file-local guarded variant that locates the pipeline under its PIGEN_SYNTAX_IF_STATEMENT parent and exposes the if node's condition expression (`as.if_statement.condition`) and its guard-signal name spans (through the expanded-source token origins, exactly as `pl_name_span` resolves them). File-local to the test file; no new production owner, no new file.
- `pl_unit`/`pl_port`/`pl_drive` cover the guarded fixtures as written: the module skeleton (clk + inputs) is built from the parse, the 1-bit guard inputs are declared with `pl_port(p, u, "en", 1)`, and `pl_drive` establishes the step-(a) identity and lowers the (now nested) pipeline node through the unchanged 5-arg seam.
- No production-source change is part of any scenario: the authors register the discriminating sections; the stand-in-replacement implementation (replacing the constant-1 guard with the enclosing procedural guard's published expression) is the later owner that depends on the accepted packet.

## Exclusions (other scoped assignments)

Reset-binding coverage (pipe_reset/pipeline_reset resetting the generated valid bits) is its own scoped mapping (plan item 8, second half; what/are/the/owners/of/the/pipeline/fsm/behavioral/test/obligations.md section 2); FSM behavior/stop (plan item 9) and CLI integration (plan item 5, does/the/pipeline/and/fsm/reach/the/compiled/output.md) are separate; the stage-computation chain (what/are/the/pipeline/stage/computation/test/scenarios.md and its accepted review) is a separate in-progress obligation. This list does not pin the per-stage next-value computation, storage payload width, backpressure or throughput.
