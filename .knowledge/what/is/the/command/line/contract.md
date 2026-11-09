---
status: green
revised_at: "2026-11-09T21:26:10+11:00"
---

The `pigen` CLI compiles one `.pigen` source through the structured pipeline (preprocess, lex, parse, semantic resolution, RTL lowering, naming, output model, SV emission) via `pigen_compile_source`. Contract: silent success — the output file is written, stdout is empty, exit 0, no notice; loud failure — a diagnostic goes to stderr, the exit code is nonzero, and no output file is written. Spanned diagnostics print `<path>:<line>:<column>: <message>`, the offending source line prefixed with four spaces, then a caret; spanless failures print `pigen: <message>`. Arguments: `pigen INPUT.pigen [-o OUTPUT.sv]`; without `-o`, a trailing `.pigen` becomes `.sv`, otherwise `.sv` is appended. Argument errors print usage and exit 2.

The structured frontend accepts the data-first dialect (`logic [7:0] buf stage`, `bit[32] buf incoming`, `int[16] fifo[8] pending`) and diagnoses transfer-first prototype spellings (`buf [7:0] stage`) as "data type must precede the transfer type". Fabric diagram options (`--diagram`, `--no-diagram`) belong to the quarantined prototype and are not accepted by the structured CLI.
