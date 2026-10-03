---
status: green
revised_at: "2026-10-03T21:17:35+10:00"
checked_at: "2026-09-14T17:13:16+10:00"
---

Run `make source-test preprocess-test transfer-type-test syntax-model-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test rtl-name-test rtl-lower-test`. C17 warning-as-error checks compile standalone binaries under /tmp. These targets exercise the unlinked foundation; ready/output/emitter/composer targets named in the approved future plan are not yet Makefile targets.

`tests/rtl_lower_test.c` uses the section harness in `tests/check.h`. Each `SECTION("name")` reports `PASS: <name>` or `FAIL: <name> at <file>:<line>: <expr>`; a failing `REQUIRE` ends only its own section, the rest still run, and the run ends with `N passed, M failed` and a nonzero exit if anything failed. Select sections by name prefix with `make rtl-lower-test T="t6-storage t6-boundary"`; `/tmp/pigen-rtl-lower-test --list` prints the names. Sections are named by task: the `t5-*` sections share one semantic/RTL model and must run together (select `t5`), while each `t6-*` section builds its own model and runs alone. The other C test files still use `assert` and stop at their first failure.

Proof:

```bash
make -s rtl-lower-test T=t6-boundary | grep -qx 'PASS: t6-boundary'
```
