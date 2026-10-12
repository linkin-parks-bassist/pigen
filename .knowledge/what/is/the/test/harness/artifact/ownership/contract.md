---
status: green
revised_at: "2026-10-13T02:41:27+11:00"
---

A test harness must build and execute each artifact inside a fresh, invocation-owned directory and clean it up, so a run observes only the binary it just compiled. Fixed shared output paths are a defect: if another build or an external editor writes the same path between the compile and execute steps, the harness executes a different binary than the one it compiled, and a source that genuinely fails its test reports pass.

The production `Makefile` C compile/execute targets — the fifteen `pigen-*-test` targets from `source-test` through `sv-emit-test` — built to and executed a fixed shared path `/tmp/pigen-<name>`. Every target's compiler options, `$(T)` section selection and exit status are correct; the output location is the defect. The SV/pipeline/waveform targets write `.sv`/`.vvp`/`.verilator` products to `/tmp` for separate simulator runs and are outside this concern.

Requirement per target: remove its own legacy fixed path `/tmp/pigen-<name>` first, then create a fresh directory (mktemp -d), compile the unchanged source list and CFLAGS into `$dir/<name>` (a compile failure must still propagate nonzero), run `$dir/<name>` preserving any section-selection argument, exit with the run's status, then remove the directory. Only the output location and its invocation change; every source list, compiler option, section selection and exit status is preserved.

The corrected form — each target removes its legacy fixed path, `mktemp -d`s a directory, compiles the unchanged sources into it, runs it, captures the exit status, removes the directory, and exits with that status (compile failure still propagates) — is the minimal superset of the mktemp form: the single added line `rm -f /tmp/pigen-<name>` per target. It is immune while a disposable external binary occupies the former shared path.

The hidden pin `tests/harness_artifact_ownership.sh` (one `contracts.json` entry, `sh tests/harness_artifact_ownership.sh`) witnesses the property on `source-test`, the canonical representative. Its original form — a NON-runnable marker (mode 644) planted at the fixed path, requiring the path absent afterward (P1) and that `make source-test` return 0 (P2) — is escapable: a "compile to the fixed path, run it, `rm -f` it" recipe passes both P1 and P2 while still compiling and executing at the shared path. The pin is being strengthened to witness invocation-OWNED execution (a runnable sentinel the run must not fire) so that escape fails and only the fresh-`mktemp -d` form passes.

`tests/**` and `Makefile` are protected harnesses; an implementation worker must not edit them, and the CointOS landing gate refuses an implementation-stage commit that changes them. The repair therefore routes through the ordinary test-contract lane, the stage that owns both `tests/**` and the `Makefile`.

Current state: UNMET on master — the production `Makefile` still uses the fixed shared path, so the defect is live. The corrected form is verified green on a retained branch (commit 85be45a on `work/harness-artifact-ownership-impl`) but not yet on master; it is routed through the test-contract lane (a packet carrying the `Makefile` repair, dependent on the strengthened pin) because the implementation lane's protected-harness gate refuses it.
