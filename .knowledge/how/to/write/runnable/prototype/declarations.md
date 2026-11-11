---
status: green
revised_at: "2026-11-11T19:58:57+11:00"
---

The structured compiler uses data-first declarations: data type, optional transfer annotation, then name. Authored examples use int[n]/uint[n]/bit, always and implicit transfer guards; logic and always_ff are compatibility surfaces (what/is/the/spec.md). Examples: int[16] reg coefficient; bit[32] fifo[8] pending[lanes]. wire/reg/logic are statics; buf/port/fifo/skid lower to elastic primitives. Unqualified module inputs are abstract and gain payload, name_valid inbound and name_ready outbound; input buf/fifo/skid is an upstream-owned boundary view.

An inline pipeline lives inside a clocked process: pipeline add begin int[16] sum, result; stage begin sum <= left + right; end stage begin result <= sum; end yield result; endpipeline, then out <= add;. Compact stage: expression; is accepted. Fields/stages expand into buf/static signals and transfers; yield writes the output.

An FSM is a module item: fsm send @(posedge clk) begin if (reset) goto idle; state idle: if (start) goto send; state send: when (destination <= source) goto idle; end. The name is optional (`fsm @(posedge clk)`). Level-zero statements run independently of the active state; ordinary reset code is a level-zero `if (reset) goto initialise;`; `state name: statement` gates one statement. A goto schedules its destination and ends this FSM invocation for the edge, suppressing every later statement; `when` performs its transfer and runs the body only on that fire (how/to/write/fsm/states.md).

A queued worker example is /tmp/pigen-job-demo/jobs.pigen: int[9] fifo[4] pending accepts requests independently; idle captures one, calculate decrements remaining and accumulates total, publish transfers total and waits for accepts(result,total) before returning idle. It is a pre-migration compatibility witness; the plan exercises the `when` surface in the showcase. The demo's domain is nonnegative request counts. Simulation checked 12 ordered results under prolonged receiver stalls.

accepts(dst,src), valid(sig), ready(sig) and peek(expr) are expressions. Transfers already enforce consuming-source validity and destination readiness; avoid redundant valid guards. Legacy validate/invalidate/flush spellings are not implemented. Child instances, parent-scope instance.port endpoints and named/anonymous fabrics are implemented; coverage/limits belong in how/to/connect/child/ports.md and what/is/an/inline/fabric.md. Transfer-first prototype spellings such as buf [7:0] sample diagnose data type must precede transfer type.
