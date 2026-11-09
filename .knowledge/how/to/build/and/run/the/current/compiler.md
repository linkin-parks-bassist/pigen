---
status: green
revised_at: "2026-11-10T07:11:43+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Run `make` to build `./pigen`, the structured frontend (pigen_compile_source through src/compile.c). Run `./pigen design.pigen -o design.sv`; compile the emitted SV alongside rtl/pigen_primitives.sv when storage primitives appear.

Use data-first declarations: `logic [7:0] buf stage;`, `bit[32] buf incoming;`, `int[16] fifo[8] pending;`. Abstract module inputs expose payload, `<name>_valid` (input) and `<name>_ready` (output); buffered outputs reverse those directions. Storage lowers to pigen_buf/pigen_port/pigen_fifo/pigen_skid. Clock/reset convention is input ports named `clk` and `reset`; a missing reset ties the primitive reset low.

Ordinary SystemVerilog remains available and compatible: multiple modules per source, `assign` statements, and standard child instantiations such as `child u(.clk(clk), .d(d), .q(q));` are retained and emitted verbatim. Structured items (declarations, transfers, pipelines, FSMs) are re-synthesized from RTL records.

Inline pipelines, FSMs and transfer conditions lower to hardware. Pipeline: `pipeline name begin ...fields... stage begin ... end ... yield field; endpipeline` inside a clocked process (compact `stage:` also works). FSM: `@(posedge clk) reset (reset) initial state` with `state ...` bodies; `if (dst <= src)` performs the transfer and branches on acceptance. Intrinsics: `accepts(dst, src)`, `valid(sig)`, `ready(sig)`, `peek(expr)`. Verified by simulation: buf chain, arithmetic pipeline, FSM counter, README sender FSM, child instance.

Still absent: structural instance/port metadata for fabric and inline fabric itself; validity actions; some expression forms (what/is/broken.md). Verification uses Icarus Verilog 13 and Verilator 5.020.
