---
status: green
revised_at: "2026-11-16T16:52:48+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An FSM sequences worker actions without serializing independent channels. Queued ingress and buffered output may operate independently; full storage propagates backpressure. The requested header is anonymous: `fsm @(posedge clk) begin ... end`. Production still accepts legacy named headers; removing that optional-name compatibility is a remaining implementation task.

Ordinary level-zero statements execute independently of the active state. `state identifier : statement` gates one ordinary statement; multiple statements use begin/end. Reset is ordinary code, for example a leading `if (reset) goto initialise;`. initialise may assign data and goto idle. There is no reset intrinsic or reset/initial header.

An executed goto schedules its destination and immediately terminates this whole FSM invocation for the clock edge, at any nesting level. No later statement in this FSM executes, even outside the enclosing branch/state. The first executed goto wins by stopping execution. Earlier writes/transfers remain effective; goto is not rollback. Other processes and FSMs continue. With no goto, state holds.

The next edge restarts execution at the top with the destination state active; it does not execute that state immediately on the goto edge. A leading reset goto suppresses later actions. Holding reset repeats the transition; initialise executes after release. Startup without asserted reset remains an open decision.

`when (destination <= source) statement` performs one transfer and executes its body only on the same fire. Its header is an assignment: the parser splits destination/value before expression parsing. Thus `when (out <= choose ? a : b)`, logical RHS expressions and a relational RHS all work without extra parentheses. An empty body still transfers. Unknown fire does not enter its body (how/are/clocked/controls/resolved.md). Malformed event headers/bodies are fatal and cannot fall back to retained SV. `if` remains a computed condition, including relational <=; goto never waits implicitly. Other when-event kinds remain unspecified.

Production resolves FSMs into the shared model. Generate loops and selected branches can own independent FSMs whose clocks, state registers and bodies use their lexical scope (how/are/generated/fsms/resolved.md). Clocks require a static signal; buffered tokens reject. Every leaf statement carries the termination condition accumulated from earlier gotos, preserving prior effects and suppressing later ones. Qualified if chains also capture entry termination and active-state conditions for their native clock-edge checks (how/are/qualified/if/statements/represented.md). Tests include tests/fsm.pigen, fsm_goto_first.pigen, when_event.pigen and tests/cores/when_value*. The latter independently checks 1536 ternary/logical/relational transfer results, stalls and exact same-fire body counts, plus invalid event rejection/output preservation. src/fsm_lower.c is a separately tested structural stage, not the production route. Remaining work is in what/is/the/plan.md.
