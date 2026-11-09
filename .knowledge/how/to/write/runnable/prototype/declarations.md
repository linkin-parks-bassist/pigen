---
status: green
revised_at: "2026-11-09T22:34:25+11:00"
---

The structured production compiler accepts data-first declarations: data type, optional transfer annotation, then name. Examples: `bit[8] buf in_packet;`, `int[16] reg coefficient;`, `bit[32] fifo[8] pending;`, `logic [7:0] buf stage;`. wire/reg/logic are statics; buf/port/fifo/skid are elastic storage and lower to `pigen_*` primitives. An unqualified module input is abstract and gains payload, `<name>_valid` input and `<name>_ready` output ports; an explicit input buf/fifo/skid is the same upstream-owned boundary view.

An inline pipeline goes inside a clocked process: `pipeline add begin int[16] sum, result; stage begin sum <= left + right; end stage begin result <= sum; end yield result; endpipeline` then `out <= add;`. The compiler expands fields and stage assignments into ordinary buf/static signals and transfers; the yield writes the pipeline output. Supported stage syntax is `stage begin ... end` (and `stage name begin ... end`).

An FSM is a module item: `fsm send @(posedge clk) reset (reset) initial idle begin state idle: if (start) goto send; state send: begin destination <= source; goto idle; end end`. The compiler creates the state register, resets it to the initial state, gates each state's actions on that state and lower-priority `!reset`, and lowers gotos to guarded state writes. `if (destination <= source)` is currently treated as a payload comparison, not the transfer-and-branch sugar.

Fabric and child instances are not implemented. Transfer-first prototype spellings such as `buf [7:0] sample;` are rejected with "data type must precede the transfer type".
