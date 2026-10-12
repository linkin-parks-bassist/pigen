---
status: green
revised_at: "2026-10-12T20:58:36+11:00"
---

A test harness must build and execute each artifact inside a fresh, invocation-owned directory and clean it up, so a run observes only the binary it just compiled. Fixed shared output paths are a defect: if another build or an external editor writes the same path between the compile and execute steps, the harness executes a different binary than the one it compiled, and a source that genuinely fails its test reports pass.

The production `Makefile` C compile/execute targets — the fifteen `pigen-*-test` targets from `source-test` through `sv-emit-test` (source-test, preprocess-test, transfer-type-test, syntax-model-test, integer-test, semantic-test, predicate-test, expression-resolve-test, expression-use-test, resolve-test, rtl-test, rtl-name-test, output-model-test, rtl-lower-test, sv-emit-test) — historically built to and executed a fixed shared path `/tmp/pigen-<name>` (e.g. `/tmp/pigen-source-test`). Every target's compiler options, `$(T)` section selection and exit status are correct; the output location is the defect. The SV/pipeline/waveform targets write their `.sv`/`.vvp`/`.verilator` products to `/tmp` for separate simulator runs and are outside this C compile/execute concern.

Requirement per target: remove the target's own legacy fixed path `/tmp/pigen-<name>` first (the target owns that path and must not leave an occupant there — this is what the ownership property's witness checks), then create a fresh directory (mktemp -d), compile the unchanged source list and CFLAGS into `$dir/<name>` (a compile failure must still propagate nonzero), run `$dir/<name>` preserving any section-selection argument, exit with the run's status, then remove the directory. Only the output location and its invocation change; every source list, compiler option, section selection and exit status is preserved.

A corrected harness was verified in scratch: each target removes its legacy fixed path, `mktemp -d`s a directory, compiles the unchanged source list into it, runs it, captures the exit status, removes the directory, and exits with that status (compile failure still propagates). All twelve non-section C targets pass and the three section targets' `$(T)` selection and `--list` are preserved. The corrected harness is immune while a disposable external binary occupies the former shared path — it removes that path up front and never executes the external binary.

The accepted red pin `tests/harness_artifact_ownership.sh` (one `contracts.json` entry, `sh tests/harness_artifact_ownership.sh`) witnesses both properties on `source-test`, the canonical representative of the fifteen. Property 1 (no fixed path) plants a distinct marker at `/tmp/pigen-source-test`, runs `make source-test`, and requires the path absent afterward; a mktemp-only recipe leaves the planted marker in place and so fails it. Property 2 (no false pass / owned execution) requires the run to report the compiled test's own status, not the marker's. The fix is the minimal superset of the mktemp form: the single added line `rm -f /tmp/pigen-<name>` per target.

`tests/**` and `Makefile` are protected harnesses; an implementation worker must not edit them directly. The fix therefore routes through the ordinary test-contract path (a separate test-contract item), not a direct worker edit.

Current state: met by the accepted implementation form (fifteen targets remove their legacy fixed path, build and run in a fresh `mktemp -d` directory, and clean it up); the red pin goes green on it.
