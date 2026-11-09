---
status: green
revised_at: "2026-11-10T06:04:39+11:00"
---

The structured production compiler accepts data-first declarations: data type, optional transfer annotation, then name. Examples: `bit[8] buf in_packet;`, `int[16] reg coefficient;`, `bit[32] fifo[8] pending;`, `logic [7:0] buf stage;`. wire/reg/logic are statics; buf/port/fifo/skid are elastic storage and lower to `pigen_*` primitives. An unqualified module input is abstract and gains payload, `<name>_valid` input and `<name>_ready` output ports; an explicit input buf/fifo/skid is the same upstream-owned boundary view.

An inline pipeline goes inside a clocked process: `pipeline add begin int[16] sum, result; stage begin sum <= left + right; end stage begin result <= sum; end yield result; endpipeline` then `out <= add;`. Compact `stage: expression;` is also accepted. Fields and stage assignments expand into ordinary buf/static signals and transfers; yield writes the pipeline output.

An FSM is a module item: `fsm send @(posedge clk) reset (reset) initial idle begin state idle: if (start) goto send; state send: begin destination <= source; if (accepts(destination, source)) goto idle; end end`. It creates the state register, resets to the initial state, gates every state action on that state and `!reset`, and lowers gotos to guarded state writes. `if (destination <= source)` is the transfer-and-branch form: it performs the transfer and branches on acceptance.

`accepts(dst, src)`, `valid(sig)`, `ready(sig)` and `peek(expr)` are expressions. `validate`, `invalidate` and `flush` are not implemented. Child instances and fabric are not implemented. Transfer-first prototype spellings such as `buf [7:0] sample;` are rejected with "data type must precede the transfer type".
