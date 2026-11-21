---
status: green
revised_at: "2026-11-21T22:54:59+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The structured frontend implements `transfer begin transfer-member+ end` inside supported clocked processes. Each direct member is `destination-expression <= source-expression;`, and the parser normalizes the group into one `PIGEN_SYNTAX_NONBLOCKING_ASSIGNMENT` whose destination and value are concatenations in member source order. `transfer begin output_packet <= result; state_mem[handle] <= next_state; end` is exactly `{output_packet, state_mem[handle]} <= {result, next_state};`.

Resolution therefore reuses the ordinary concatenated-transfer analysis: one guard, one aggregate resize and width check, one readiness/validity decision and one fire event. Every member updates or none does. Repeated sources consume their complete base once, and reads needed by member addresses or selects participate in validity and ownership. Individual member widths and counts may differ because the whole source is resized to the destination aggregate before splitting. Constants and statics keep their normal laws.

Members are assignments only. Control flow surrounds the group, and an empty group, a missing or repeated `<=`, an empty destination or source, a missing trailing semicolon, a control-flow statement inside the group, or a missing `end` diagnoses at the offending token in the original source. Recognition is token-based, insensitive to whitespace and comments, and contextual: outside the complete `transfer begin` form inside such a process, `transfer` remains an ordinary SystemVerilog identifier. Guarded blocks, `when` bodies, FSM states and pipeline stages accept the group; a pipeline stage lowers its members as that stage's field writes and shares the stage packet handshake.

The README fan-out example declares `uint[32] buf output_packet` and `uint[32] fifo[8] history`; both receive each `result` in one transfer while independent readers drain the buffer and FIFO at their own rates. A complete module containing the block compiles, passes fatal Verilator lint and is checked with independently stalled readers by tests/transfer_block_test.py.
