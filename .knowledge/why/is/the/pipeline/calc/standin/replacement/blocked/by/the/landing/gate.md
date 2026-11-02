---
status: green
revised_at: "2026-11-02T22:40:21+11:00"
---

The pipeline stage-computation stand-in replacement (plan item 1) is implemented and its acceptance is blocked only by a stale gate composition, not by a product defect.

## What is implemented

Commit e586075 (branch work/pipeline-calc-standin-replacement, base 3fc4f3c) changes ONLY src/pipeline_lower.c: each pipeline stage's next-value equation now publishes the stage's actual computed expression (stage_expr_build, stage_item_tokens, carry_span_for_field) instead of the constant-0 stand-in, and a computed value carried across a boundary renders on the yield with its operators present. Verified against the accepted calc binding tests: with four calc sections (calc-arithmetic, calc-bitwise, calc-first-stage, calc-cross-boundary) grafted onto e586075's tree, `make pipeline-lower-test` is 18 passed, 0 failed; the worker verified all six (arithmetic/bitwise/first-stage/cross-boundary/no-standin/concat) green.

## Why the gate refused (the false block)

The candidate e586075 is based on 3fc4f3c, which PRE-dates c2827a5 ("Stop registering seven contract commands that master already fails"). c2827a5 is on master and removed the seven corpus prototype-dialect targets (lint_fatal_pin, fabric-test, core-language-test, vertical-slice-test, coslice-test, coslice-typedef-test, biquad-bank-test, coslice-xprop-test) from tests/contracts.json because they fail on master as per-subsystem migration debt (what/is/broken.md) and "check nothing." e586075's own contracts.json therefore still carries those seven stale targets and none of the six calc entries. The landing gate ran the candidate's own stale test tree (tree 7cb48046, 116 tests, 7 corpus, 0 calc) instead of the accepted binding set (pinned main + the six calc packets), so it reported the seven pre-existing corpus failures and refused. src/pipeline_lower.c is NOT in the production pigen link set (Makefile links src/pipeline.c), so e586075 cannot affect the corpus targets at all.

## How to unblock

Rebase e586075 onto current master (0b6b34f). e586075's test files are byte-identical to master's (0 calc, corpus already removed by c2827a5), and master does not touch src/pipeline_lower.c in the range e586075..master (seven commits, no conflict), so the rebase is clean: the candidate becomes current master + the src/pipeline_lower.c change. The gate then runs the accepted set (pinned main, no corpus, + six calc packets) and e586075 passes all of it, so it lands. No product change is required; the work is done. The landing owner should also recompute plan item 1 (the six sections are accepted, the stand-in replacement is implemented and being landed) and what/is/broken.md.
