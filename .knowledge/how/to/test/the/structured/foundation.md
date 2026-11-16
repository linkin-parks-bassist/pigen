---
status: green
revised_at: "2026-11-16T03:03:27+11:00"
checked_at: "2026-11-10T06:05:31+11:00"
---

Use focused structured-owner targets during edits and make verify at checkpoints (how/to/run/repository/verification.md). Source, preprocessing, transfer/type, syntax, semantic, expression, resolution, RTL, naming, output, routing and pipeline/FSM targets compile C witnesses against their owning source files. Targets use fresh mktemp directories and remove them afterward; some also remove their legacy fixed temporary path before running.

Individual owner gates include source-test, preprocess-test, transfer-type-test, syntax-model-test, generate-syntax-test, generate-semantic-test, generated-instance-test, fsm-syntax-test, integer-test, semantic-test, predicate-test, expression-resolve-test, expression-use-test, resolve-test, rtl-test, rtl-name-test, output-model-test, route-test, route-law-test, route-vdo-test, route-stall-test, pipeline-output-test, pipeline-stage-test, pipeline-lower-test, fsm-lower-test and compile-test.

make rtl-lower-test runs all 24 sections of tests/rtl_lower_test.c, covering type/expression lowering, declaration endpoints, storage, transfer ownership and process ordering. FIFO depth assertions follow the primitive's signed 32-bit interface rather than the authored argument width (what/is/elastic/rtl/task/six.md). make sv-emit-test runs five sections: skeleton, expression rendering, no-partial-output, combinational statements and golden output. Their former endpoint/fire/instance expectations do not justify ignoring a current failure.

tests/rtl_lower_test.c and other section-harness files such as output_test.c, route tests and sv_emit_test.c use tests/check.h. Each SECTION reports PASS/FAIL; a failure ends only its section, and T= selects sections by prefix. Assert-based targets stop at their first failure. make pipeline-identity-test T=<section> selects shape, field-buf, scope or yield; no argument prints usage and exits 2.

make expression-resolve-test and semantic-test check typed constants and runtime construction, including no-partial-publication rules. make generate-semantic-test and generated-instance-test check lexical elaboration and specialization. make four-to-two-chunk-test compares native SV two-state conversion across chunk boundaries, X/Z patterns, nested casts and an overridden child; static-local-initializer-test compares startup and assignment contexts with native SV. unpacked-initializer-test compares contextual arrays, bounds/default sizing and startup/entry initialization with native SV. automatic-local-initializer-test compares per-entry defaults and initialization with native automatic function activations, including loop/array/record lifetimes and escaping negatives. named-procedural-block-test compares lexical named blocks, qualified locals and ordered execution with native SV. numerical-generate-test compares numerical-only explicit/implicit regions and overridden children across uint/int/bit declarations and casts, preserving ordinary SV ranges and indexed values. Hardware protocol, backpressure and core/fabric witnesses remain required alongside C unit checks.

Proof:

```bash
make -s compile-test | grep -q 'All compile tests passed'
```
