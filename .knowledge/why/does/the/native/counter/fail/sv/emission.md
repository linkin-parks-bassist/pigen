---
status: green
revised_at: "2026-11-09T21:26:35+11:00"
---

The original counter failure was the mixed token/byte opaque cursor: commit_declaration advanced parser->opaque_byte_cursor past the ANSI ports while parse_clocked_process advanced only the token cursor, so parse_module duplicated `);` and the process source and validate_coverage rejected the overlap.

pigen-fork fixes this in src/syntax.c: add_opaque emits a byte gap from one cursor to the next parsed item, and every structured item advances that cursor. OPAQUE spans are contiguous. The output build walk also publishes module objects, instances, equations and processes, so a clocked process is no longer dropped from emitted SV. The structured compiler now compiles the native `uint[25] reg` counter and emits its always block. Verified: ./pigen on pigen-experiments/blink/counter.pigen exits 0 and writes the clocked counter.
