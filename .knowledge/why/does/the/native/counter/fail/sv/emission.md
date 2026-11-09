---
status: green
revised_at: "2026-11-09T15:47:31+11:00"
---

The native uint[25] reg counter with a posedge always_ff process fails on compiler snapshot d80bb41d5fe71d1920029f6df99735a4230175ed because syntax parsing mixes token and byte cursors for retained opaque source text. commit_declaration advances parser->opaque_byte_cursor through the final ANSI port identifier. parse_clocked_process records the intervening `);` via add_opaque and advances only the token-based opaque_cursor. Neither add_opaque nor parse_clocked_process advances opaque_byte_cursor. parse_module finally records source from that stale byte cursor to source_length, duplicating `);` and including the structured process again. validate_coverage in src/sv_emit.c rejects the overlap, and the CLI reports only `pigen: SystemVerilog emission failed`.

Controlled probes through the copied unmodified bin/pigen in /home/david/Projects/pigen-experiments:
- blink/counter.pigen contains the full always_ff counter and fails emission (exit 1).
- blink/counter_always.pigen changes only always_ff to always and fails with the identical diagnostic (exit 1). parse_clocked_process accepts both keywords through the same code path.
- blink/counter_empty.pigen retains the identical module header but removes the body and compiles (exit 0). Its generated build/counter_empty.sv also passes `iverilog -g2012 -s counter -tnull`. This isolates the observed failure from the header's native uint[25] reg declaration; it does not establish working counter behavior.

The full counter's observed opaque intervals are [0,15), [30,32), [49,51), [76,78), [76,196); the duplicated text is `);` at [76,78). Evidence: temporary diagnostic instrumentation of the isolated snapshot's emitter plus independent reading of src/syntax.c add_opaque, commit_declaration, parse_clocked_process and parse_module. Instrumentation was removed from source; the copied bin/pigen is the original unmodified snapshot build.

Clearing this overlap alone does not establish a working counter: src/output.c node_identity maps only modules and signal declarators, so the build walk does not publish RTL_PROCESS items for clocked processes. Fix and simulate the complete emitted behavior before programming hardware.
