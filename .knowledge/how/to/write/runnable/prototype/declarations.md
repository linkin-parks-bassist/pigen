---
status: green
revised_at: "2026-10-31T15:51:30+11:00"
---

Production ./pigen uses the structured frontend. Transfer-first prototype declarations such as `buf [7:0] sample;` are rejected with "data type must precede the transfer type". Use data-first syntax, for example `logic [7:0] buf sample;` (tests/owned_signal.pigen). This is not evidence of complete executable semantics: the master CLI can accept declarations and emit plain logic while omitting transfer/storage behavior.

The examples/ and tests/ corpus still contains prototype syntax, including transfer-first ports and inline pipelines. Those files are migration evidence, not a runnable production-language recipe. Consult does/the/structured/frontend/run/in/production.md and what/is/broken.md before relying on them. There is no approved alternate dialect, fallback or per-file compiler choice.

For an isolated CLI probe, build a pinned source snapshot outside active worktrees, run `./pigen INPUT.pigen -o OUTPUT.sv`, inspect the emitted behavior, and simulate relevant semantics; successful compilation alone does not establish correctness.
