---
status: green
revised_at: "2026-10-29T07:43:22+11:00"
---

## Scope

One test obligation from the pipeline contract: **a pipeline stage performs its specified nontrivial next-value computation on concrete input** — each stage's `<=` assignments define the outgoing packet as expressions over the immutable incoming fields (the fields earlier stages wrote) and enclosing module signals, and the parent-module lowering publishes those computations, not placeholders. Halting/stop behavior and FSM contract tightening are separate scoped mapping assignments in the broader test-contract frontier; CLI wiring is out of scope (already landed).

## Spec and contract sources

- `what/is/the/spec.md`: "Pipeline stages read immutable incoming fields and define outgoing packet state; fields are buf, yield exposes final outgoing value".
- `how/do/pipeline/stage/reads/work.md`: each stage field read sees its immutable incoming packet; `<=` computes outgoing fields visible only in the following stage; the first stage takes enclosing signals plus stage-local combinational values.
- `what/is/the/pipeline/parent/module/lowering.md`: step (c) lowering; per-stage combinational next-value equations over the immutable incoming fields.
- `what/are/the/remaining/architecture/migration/gates.md`: acceptance — every accepted specified construct has behavioral coverage.
- Seam: `tests/pipeline_lower_test.c` (target `pipeline-lower-test`, Makefile section entry per `SECTION` in `tests/contracts.json`); owner `include/pigen/pipeline_lower.h` / `src/pipeline_lower.c`; RTL record model `include/pigen/rtl.h` (`pigen_rtl_equation`: origin, destination, value; expression kinds `PIGEN_RTL_EXPR_OBJECT/INTEGER/BINARY/UNARY/CONCATENATION`); terminal rendering `pigen_sv_render_expression`.

## Current coverage

`pipeline-lower-test` section (2) `next-value-equations` pins only equation COUNT (`equation_count >= stage_count`) on a pass-through pipeline (`a <= x`, `b <= a`); sections (1), (3), (4), (5) pin storage, yield write, member order and guard/handshake shape. No section pins the COMPUTATION: `src/pipeline_lower.c` today publishes each stage's next-value equation with a constant-0 value (a stand-in for the value computed over the incoming fields), so a lowering that computes nothing passes the manifest. This list fills that gap; after it, the implementation must replace the stand-in with the stage's published expressions.

## Test scenarios

One line = one later test-author scenario, registered as its own `SECTION` in `tests/pipeline_lower_test.c` with a `tests/contracts.json` entry, built on the established seam helpers (`pl_parse`, `pl_unit`, `pl_port`, `pl_drive`, `pigen_sv_render_expression`).

1. Arithmetic next value over an incoming field: a legal 2-stage pipeline whose stage 2 computes `b <= a + 8'h3D` over the immutable incoming 8-bit field `a` (stage 1: `a <= x`) lowers to rc == 0 and publishes, for stage 2, a combinational next-value equation whose value is a binary `+` expression (PIGEN_RTL_EXPR_BINARY) over the incoming field's object and the integer 8'h3D — not an object pass-through and not a constant.
2. Bitwise next value over two incoming fields: a legal 2-stage pipeline whose stage 2 computes `c <= a & b` over two 8-bit incoming fields `a`, `b` (stage 1 writes both from external inputs) lowers to rc == 0 and publishes stage 2's next-value equation with a binary bitwise-and expression naming BOTH incoming field objects.
3. First-stage computation over an enclosing signal: a legal 2-stage pipeline whose first stage computes `a <= d + 8'h01` over the enclosing module 8-bit input `d` (no pipeline-field read in stage 1) lowers to rc == 0 and publishes stage 1's next-value equation whose value references the enclosing signal `d`'s object in the computed expression.
4. Computation visible across a boundary: a legal 2-stage pipeline whose stage 1 computes `a <= x + 8'h10` and stage 2 computes `b <= a` (pass-through of the computed field) lowers to rc == 0 and the yield write's source chain renders through the terminal SV emission with the computed expression (the `+`) present for the carried field — the computed value, not an uncomputed placeholder, is what the following stage sees and the packet carries.
5. Constant stand-in is no longer the published value: a legal 2-stage pipeline with nontrivial stage computations lowers to rc == 0 and NO published stage next-value equation has an integer-constant value (PIGEN_RTL_EXPR_INTEGER) — the stand-in the manifest must now discriminate.
6. Concatenation next value over incoming fields: a legal 2-stage pipeline whose stage 2 computes `c <= {a, b}` over two 8-bit incoming fields lowers to rc == 0 and publishes stage 2's next-value equation whose value is a PIGEN_RTL_EXPR_CONCATENATION naming both incoming field objects in source order.

## Minimal fixture prerequisites

- `tests/pipeline_lower_test.c` seam helpers (`pl_parse`/`pl_unit`/`pl_port`/`pl_drive`) cover scenarios 1–6 as written (multi-`<=` items in one stage, external ports, stage-local field reads); no new fixture owner is required.
- `pigen_sv_render_expression` + `pigen_rtl_assign_names` (SOURCE role) cover the rendered-expression check in scenario 4, exactly as section (4) emitted-member-order already uses them.
- No production-source change is part of any scenario: the authors register the discriminating sections; the stand-in-replacement implementation is the later owner that depends on the accepted packet.

## Exclusions (other scoped assignments)

Halting/stop-condition coverage, FSM test-contract tightening, and CLI integration belong to the broader test-contract frontier (CLI reachability is owned by the active CLI-Contracts operator assignment); this list does not pin reset bindings, guard polarity, backpressure or throughput.
