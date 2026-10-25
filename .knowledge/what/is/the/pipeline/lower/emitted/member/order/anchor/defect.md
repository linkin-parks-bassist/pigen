---
status: green
revised_at: "2026-10-26T00:19:28+11:00"
---

The accepted (c2) pipeline-lower manifest (tests/pipeline_lower_test.c, on master 38b6968) has a defective emitted-member-order anchor. Its anchor is a single stage `x <= x` over pipeline field `x` — a first-stage self-read.

The established first-stage law (what/is/the/pipeline/stage/execution/laws/test/manifest.md, section first-stage-external-reads, green on master; owner src/pipeline_stage.c) diagnoses any first-stage pipeline-field read as rc == -1, including a self-read: the owner's value-token walk classifies every identifier after the `<=` destination as a field read, so a self-read RHS reads the very field being written (the self-read red form `a <= a` is pinned green in pipeline-stage-test on master). The emitted-member-order anchor requires rc == 0 for exactly such a self-read.

The five manifest sections are therefore mutually unsatisfiable as written:
- A lowering that enforces the step-(b) first-stage law — which the manifest's own buf-storage red (first-stage field read `a <= b`) and next-value-equations red (unequal-total-$bits boundary) REQUIRE(rc == -1) — rejects `x <= x` and fails the anchor's REQUIRE(rc == 0).
- A lowering that skips that validation accepts the anchor (rc == 0) but then lowers the buf-storage and next-value-equations reds to rc == 0 and fails their REQUIRE(rc == -1).

Verified by building the (c3) implementation (work branch, e2be52b) against the (c2) manifest — 4 sections green, emitted-member-order red at REQUIRE(rc == 0) — and a stage-law-skipping variant, which flips emitted-member-order green and turns buf-storage and next-value-equations red. The defect is a test-witness construction error in the anchor, not a lowering defect and not an ambiguity in the product law.

The anchor must be re-scoped to a legal first stage: its first stage may read only an enclosing/module-scope signal or a stage-local value, never a pipeline field. The correction must preserve the two-yielded-field `yield {x, y}` pin (x 8-bit MSB, y 16-bit LSB, source order, the rendered SV `{x,y}` not `{y,x}`, child_count 2) and the two source-level reds. The (c3) implementation is blocked on the independent acceptance of a test-contract correction of this anchor; once accepted, the corrected manifest is the binding (c3) contract and (c3) implementation then its landing (c4) remain (what/is/the/pipeline/parent/module/lowering.md).
