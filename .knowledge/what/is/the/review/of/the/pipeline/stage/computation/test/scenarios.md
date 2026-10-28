---
status: green
revised_at: "2026-10-29T08:06:26+11:00"
---

Independent review of the pipeline-stage computation test scenario list at what/are/the/pipeline/stage/computation/test/scenarios.md (committed 6037b5d, landed on master). Scope: the single pipeline test obligation that a stage performs its specified nontrivial next-value computation on concrete input, enforced at the parent-module lowering seam (tests/pipeline_lower_test.c, target pipeline-lower-test, one SECTION plus one tests/contracts.json entry per scenario).

## Verdict: ACCEPTED

Each line names a concrete setup (a legal 2-stage pipeline with the specified `<=`), one action (lower through the established seam), and one observable expected result (a published next-value equation whose value is the specified expression). Every line is reachable through the established seam APIs, and no line pins halting/stop, FSM or CLI behavior.

## Per-line check (setup / action / observable result / seam reachability)

1. Arithmetic over an incoming field (`b <= a + 8'h3D`): setup legal 2-stage, stage 2 reads immutable `a`; action lower; expects a PIGEN_RTL_EXPR_BINARY `+` over the incoming field's object and the integer 8'h3D. Reachable: pl_parse/pl_unit/pl_port/pl_drive drive the pipeline to a pigen_rtl_model; pigen_rtl_equation_get/pigen_rtl_expr_get/pigen_rtl_object_get inspect the published equation. One job: one expression-kind assertion over one incoming field.
2. Bitwise over two incoming fields (`c <= a & b`): setup legal 2-stage, stage 1 writes both `a`,`b` from external inputs, stage 2 reads both; expects a binary bitwise-and naming BOTH incoming field objects. Reachable as 1. One job: the two-object binary assertion.
3. First stage over an enclosing signal (`a <= d + 8'h01`): setup stage 1 reads enclosing input `d` (no pipeline-field read); expects stage 1's equation value to reference `d`'s object. Reachable as 1, using pl_port for `d`. One job: the enclosing-signal reference assertion.
4. Computation carried across a boundary (stage 1 `a <= x + 8'h10`, stage 2 `b <= a`): setup a computed field then pass-through; expects the yield write's source chain to render through the terminal SV emission with the computed `+` present. Reachable: pigen_rtl_assign_names (PIGEN_RTL_NAME_SOURCE) + pigen_sv_render_expression, exactly as the existing emitted-member-order section (tests/pipeline_lower_test.c) already uses them. One job: one rendered-expression check.
5. No constant stand-in: setup a legal 2-stage pipeline with nontrivial computations; expects NO published stage next-value equation to have a PIGEN_RTL_EXPR_INTEGER value. This is the line that discriminates the current constant-0 stand-in (src/pipeline_lower.c line 550, pigen_rtl_expr_add_integer(...,0,...)). Reachable as 1 (iterate equations, assert kind != INTEGER). One job: the stand-in exclusion assertion.
6. Concatenation over incoming fields (`c <= {a, b}`): setup legal 2-stage, stage 2 reads both; expects a PIGEN_RTL_EXPR_CONCATENATION naming both incoming field objects in source order. Reachable as 1. One job: the concatenation assertion.

## Coverage assessment

The list collectively enforces the computation obligation: the published next-value expression must be the stage's specified expression over the immutable incoming fields and enclosing signals. Lines 1–4 and 6 pin the positive computation over each expression form (binary arithmetic, binary bitwise, enclosing-signal, cross-boundary carry, concatenation) and the object identities they must reference. Line 5 pins the negative: no equation may carry the constant-0 stand-in the seam currently publishes, so the existing next-value-equations section (which pins only equation_count >= stage_count) is no longer sufficient and the list is discriminating against src/pipeline_lower.c's present behavior. The boundaries hold: halting/stop-condition coverage, FSM test-contract tightening and CLI wiring are named as separate scoped assignments and are not pinned by any line.

## Reviewed list (the six accepted scenario lines)

1. Arithmetic next value over an incoming field: a legal 2-stage pipeline whose stage 2 computes `b <= a + 8'h3D` over the immutable incoming 8-bit field `a` (stage 1: `a <= x`) lowers to rc == 0 and publishes, for stage 2, a combinational next-value equation whose value is a binary `+` expression (PIGEN_RTL_EXPR_BINARY) over the incoming field's object and the integer 8'h3D — not an object pass-through and not a constant.
2. Bitwise next value over two incoming fields: a legal 2-stage pipeline whose stage 2 computes `c <= a & b` over two 8-bit incoming fields `a`, `b` (stage 1 writes both from external inputs) lowers to rc == 0 and publishes stage 2's next-value equation with a binary bitwise-and expression naming BOTH incoming field objects.
3. First-stage computation over an enclosing signal: a legal 2-stage pipeline whose first stage computes `a <= d + 8'h01` over the enclosing module 8-bit input `d` (no pipeline-field read in stage 1) lowers to rc == 0 and publishes stage 1's next-value equation whose value references the enclosing signal `d`'s object in the computed expression.
4. Computation visible across a boundary: a legal 2-stage pipeline whose stage 1 computes `a <= x + 8'h10` and stage 2 computes `b <= a` (pass-through of the computed field) lowers to rc == 0 and the yield write's source chain renders through the terminal SV emission with the computed expression (the `+`) present for the carried field — the computed value, not an uncomputed placeholder, is what the following stage sees and the packet carries.
5. Constant stand-in is no longer the published value: a legal 2-stage pipeline with nontrivial stage computations lowers to rc == 0 and NO published stage next-value equation has an integer-constant value (PIGEN_RTL_EXPR_INTEGER) — the stand-in the manifest must now discriminate.
6. Concatenation next value over incoming fields: a legal 2-stage pipeline whose stage 2 computes `c <= {a, b}` over two 8-bit incoming fields lowers to rc == 0 and publishes stage 2's next-value equation whose value is a PIGEN_RTL_EXPR_CONCATENATION naming both incoming field objects in source order.

## Accepted seam APIs (relies_on for the test-contract authors)

pigen_pipeline_lower (include/pigen/pipeline_lower.h, the 5-arg entry point), pigen_rtl_equation_get, pigen_rtl_expr_get, pigen_rtl_object_get (include/pigen/rtl.h); line 4 additionally uses pigen_rtl_assign_names (PIGEN_RTL_NAME_SOURCE) and pigen_sv_render_expression (include/pigen/sv_emit.h). File-local seam helpers pl_parse/pl_unit/pl_port/pl_drive/pl_stage_nodes (tests/pipeline_lower_test.c) are the established fixture path; no new fixture owner is required.
