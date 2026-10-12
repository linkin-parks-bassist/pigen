---
status: green
revised_at: "2026-10-12T14:17:26+11:00"
---

A test harness must build and execute each artifact inside a fresh, invocation-owned directory and clean it up, so a run observes only the binary it just compiled. Fixed shared output paths are a defect: if another build or an external editor writes the same path between the compile and execute steps, the harness executes a different binary than the one it compiled, and a source that genuinely fails its test reports pass.

The production `Makefile` C compile/execute targets — the fifteen `pigen-*-test` targets from `source-test` through `sv-emit-test` — build to and execute a fixed shared path `/tmp/pigen-<name>` (e.g. `/tmp/pigen-source-test`). Every target's compiler options, `$(T)` section selection and exit status are correct; only the output location is the defect. The SV/pipeline/waveform targets write their `.sv`/`.vvp`/`.verilator` products to `/tmp` for separate simulator runs and are outside this C compile/execute concern.

A corrected harness was verified in scratch: each target `mktemp -d`s a directory, compiles the unchanged source list into it, runs it, captures the exit status, removes the directory, and exits with that status (compile failure still propagates). All twelve non-section C targets pass and the three section targets' `$(T)` selection and `--list` are preserved. The corrected harness also exposes an independently failing implementation (a one-token defect in `src/source.c` line-start computation that `tests/source_test.c` catches) and stays immune while a disposable external binary occupies the former shared path — it never executes the external binary.

`tests/**` and `Makefile` are protected harnesses; an implementation worker must not edit them directly. The fix therefore routes through the ordinary test-contract path (a separate test-contract item), not a direct worker edit.
