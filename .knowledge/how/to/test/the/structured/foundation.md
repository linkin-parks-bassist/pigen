---
status: green
revised_at: "2026-11-10T06:05:10+11:00"
checked_at: '2026-11-10T06:05:31+11:00'
---

The structured suite is split between green owner tests and two targets whose sections still pin the superseded endpoint/fire/instance contract.

Green and passing with no argument or focused sections: source-test, preprocess-test, transfer-type-test, syntax-model-test, fsm-syntax-test, integer-test, semantic-test, predicate-test, expression-resolve-test, expression-use-test, resolve-test, rtl-test, rtl-name-test, output-model-test, route-test, route-law-test, route-vdo-test, route-stall-test, pipeline-output-test, pipeline-stage-test, pipeline-lower-test, fsm-lower-test, sv-emit-test's t10-skeleton/t10-expr-render, compile-test. These compile each artifact in a fresh `mktemp -d`, run it, and remove the directory; the legacy fixed `/tmp/pigen-<name>` path is removed first.

Still red pending rebaseline: nine `rtl-lower-test` sections pin the old storage-control/invalid-control contract and update-value-equals-fire behavior (t6-boundary, t6-storage-matrix, t6-storage-instances, t6-storage-fifo-depth, t6-storage-counts, t8-module, t8-fire-identity, t8-guard, t8-ownership); two `sv-emit-test` sections pin positional instance text and OPAQUE-must-cover-structured-spans. Do not treat those failures as regressions of the new datapath.

`pipeline-identity-test` is parameterized: `make pipeline-identity-test T=<section>` for `<section>` in {shape, field-buf, scope, yield}; no argument prints usage and exits 2. `tests/rtl_lower_test.c` and the other section-harness files (`output_test.c`, route tests, `sv_emit_test.c`) use `tests/check.h`: each SECTION reports PASS/FAIL, a failure ends only its section, and select sections by name prefix with `T=`. The remaining assert-based targets stop at their first failure.

Proof:

```bash
make -s compile-test | grep -q 'All compile tests passed'
```
