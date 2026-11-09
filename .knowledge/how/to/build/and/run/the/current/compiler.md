---
status: green
revised_at: "2026-11-09T22:34:37+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Run `make` to build `./pigen`, the structured frontend (pigen_compile_source through src/compile.c). Run `./pigen design.pigen -o design.sv`; compile the emitted SV alongside rtl/pigen_primitives.sv when storage primitives appear.

Use data-first declarations: `logic [7:0] buf stage;`, `bit[32] buf incoming;`, `int[16] fifo[8] pending;`. Abstract module inputs expose payload, `<name>_valid` (input) and `<name>_ready` (output); buffered outputs expose the same handshake with directions reversed. Storage lowers to pigen_buf/pigen_port/pigen_fifo/pigen_skid. The module clock/reset convention is input ports named `clk` and `reset`; a missing reset ties the primitive reset low.

Inline pipelines and FSMs now lower to hardware. A pipeline is written as `pipeline name begin ...fields... stage begin ... end ... yield field; endpipeline` inside a clocked process. An FSM is a module item with `@(posedge clk) reset (reset) initial state` and `state ...` bodies containing assignments and gotos. Verified by simulation: three-stage buf chain, three-stage arithmetic pipeline, FSM counter.

Still absent: child instances and fabric; the transfer-condition sugar and accepts/peek/validity intrinsics (what/is/broken.md). Verification uses Icarus Verilog 13 and Verilator 5.020.
