---
status: green
revised_at: "2026-11-10T06:04:39+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Run `make` to build `./pigen`, the structured frontend (pigen_compile_source through src/compile.c). Run `./pigen design.pigen -o design.sv`; compile the emitted SV alongside rtl/pigen_primitives.sv when storage primitives appear.

Use data-first declarations: `logic [7:0] buf stage;`, `bit[32] buf incoming;`, `int[16] fifo[8] pending;`. Abstract module inputs expose payload, `<name>_valid` (input) and `<name>_ready` (output); buffered outputs expose the same handshake with directions reversed. Storage lowers to pigen_buf/pigen_port/pigen_fifo/pigen_skid. The module clock/reset convention is input ports named `clk` and `reset`; a missing reset ties the primitive reset low.

Inline pipelines, FSMs and transfer conditions lower to hardware. Pipeline: `pipeline name begin ...fields... stage begin ... end ... yield field; endpipeline` inside a clocked process (compact `stage:` also works). FSM: a module item with `@(posedge clk) reset (reset) initial state` and `state ...` bodies with assignments/gotos; `if (dst <= src)` performs the transfer and branches on acceptance. Intrinsics: `accepts(dst, src)`, `valid(sig)`, `ready(sig)`, `peek(expr)`. Verified by simulation: buf chain, arithmetic pipeline, FSM counter, README sender FSM.

Still absent: child instances and fabric; validity actions (`validate`, `invalidate`, `flush`); some alias/conditional expression forms (what/is/broken.md). Verification uses Icarus Verilog 13 and Verilator 5.020.
