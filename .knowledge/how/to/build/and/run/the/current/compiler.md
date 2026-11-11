---
status: green
revised_at: "2026-11-11T15:51:28+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Run `make` to build `./pigen`, the structured frontend (pigen_compile_source through src/compile.c). Run `./pigen design.pigen -o design.sv`; compile the emitted SV alongside rtl/pigen_primitives.sv when storage primitives appear.

Use data-first declarations: `logic [7:0] buf stage;`, `bit[32] buf incoming;`, `int[16] fifo[8] pending;`. Abstract module inputs expose payload, `<name>_valid` (input) and `<name>_ready` (output); buffered outputs reverse those directions. Storage lowers to pigen_buf/pigen_port/pigen_fifo/pigen_skid. Clock/reset convention is input ports named `clk` and `reset`; a missing reset ties the primitive reset low.

Ordinary SystemVerilog remains available and compatible: multiple modules per source, `assign` statements, and standard child instantiations such as `child u(.clk(clk), .d(d), .q(q));` preserve behavior; instance connections may be expanded into named payload/valid/ready wiring. Structured items (declarations, transfers, pipelines, FSMs) are re-synthesized from RTL records.

Inline pipelines, FSMs and transfer conditions lower to hardware. Pipeline: `pipeline name begin ...fields... stage begin ... end ... yield field; endpipeline` inside a clocked process (compact `stage:` also works). FSM: `@(posedge clk) reset (reset) initial state` with `state ...` bodies; `if (dst <= src)` performs the transfer and branches on acceptance. Intrinsics: `accepts(dst, src)`, `valid(sig)`, `ready(sig)`, `peek(expr)`. Verified by simulation: buf chain, arithmetic pipeline, FSM counter, README sender FSM, child instance.

Structural instance/port metadata, inline fabrics, validity actions and generated parent endpoints are implemented. Use instance.port in sibling connections, parent transfers or fabrics; see how/to/connect/child/ports.md for syntax and remaining coverage. Current gaps belong in what/is/broken.md. Verification uses Icarus Verilog 13 and Verilator 5.020.
