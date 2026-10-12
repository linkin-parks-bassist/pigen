---
status: green
revised_at: "2026-10-12T15:46:08+11:00"
---

The fifteen C compile/execute Makefile targets (source-test, preprocess-test, transfer-type-test, syntax-model-test, integer-test, semantic-test, predicate-test, expression-resolve-test, expression-use-test, resolve-test, rtl-test, rtl-name-test, output-model-test, rtl-lower-test, sv-emit-test) must each build and execute their artifact in a fresh, invocation-owned directory created with `mktemp -d`, and remove that directory when done.

Why: building to a fixed host path such as `/tmp/pigen-<name>` and then executing that same path lets an external build or editor overwrite the binary between the compile and the execute steps. The harness would then run a different binary than the one it compiled, so a source that genuinely fails its test reports pass (false pass).

Requirement per target: create a fresh directory, compile the unchanged source list and CFLAGS into `$dir/<name>` (a compile failure must still propagate nonzero), run `$dir/<name>` preserving any section-selection argument, exit with the run's status, then remove the directory. Only the output location and its invocation change; every source list, compiler option, section selection and exit status is preserved.

Out of scope: the SV/pipeline/waveform targets (their `/tmp` `.sv`/`.vvp`/`.verilator` products feed separate simulator runs), the production `./pigen` build, and `tests/contracts.json` argv (the daemon landing manifest calls `make <target>` and is unaffected).

Current state: unmet. All fifteen targets still build to and execute the fixed `/tmp/pigen-<name>` path.
