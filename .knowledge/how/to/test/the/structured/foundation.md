---
status: green
revised_at: "2026-10-19T17:17:54+11:00"
checked_at: '2026-10-19T17:24:35+11:00'
---

Run `make source-test preprocess-test transfer-type-test syntax-model-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test rtl-name-test output-model-test rtl-lower-test route-test route-law-test sv-emit-test compile-test`. These C17 warning-as-error targets each compile their artifact in a fresh `mktemp -d` invocation-owned directory, run it, then remove the directory, so a run observes only the binary it just built (the artifact-ownership contract owns the why); the legacy fixed `/tmp/pigen-<name>` path is `rm -f`'d first and no executable is left at it. They exercise the unlinked structured foundation; the prototype's SV, pipeline and waveform targets are separate.

`tests/rtl_lower_test.c` uses the section harness in `tests/check.h`. Each `SECTION("name")` reports `PASS: <name>` or `FAIL: <name> at <file>:<line>: <expr>`; a failing `REQUIRE` ends only its own section, the rest still run, and the run ends with `N passed, M failed` and a nonzero exit if anything failed. Select sections by name prefix with `make rtl-lower-test T="t6-storage t6-boundary"`; `--list` prints the names without running them (run the freshly built binary in its temp directory, or select a prefix to run the matching sections). Sections are named by task: the `t5-*` sections share one semantic/RTL model and are run together (select `t5`), while each `t6-*` section builds its own model and runs alone. `tests/output_test.c`, `tests/route_test.c`, `tests/route_law_test.c` and `tests/sv_emit_test.c` use the same section harness; the remaining targets (source, preprocess, transfer-type, syntax-model, integer, semantic, predicate, expression-resolve, expression-use, resolve, rtl, rtl-name, compile) still use `assert` and stop at their first failure.

Proof:

```bash
make -s rtl-lower-test T=t6-boundary | grep -qx 'PASS: t6-boundary'
```
