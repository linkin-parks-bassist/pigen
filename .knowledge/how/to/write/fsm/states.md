---
status: green
revised_at: "2026-11-11T18:48:06+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An FSM sequences a worker's actions without serializing independent channels. FIFO ingress can queue requests while the worker calculates, and a buffered output can hold a completed answer while the worker starts another job. Full storage eventually propagates backpressure. See where/are/behavioral/examples.md for the simulated queued worker.

The specified outer form is `fsm @(posedge clk) begin ... end`, optionally named. Ordinary level-zero statements execute independently of the active state. `state identifier : statement` gates one ordinary statement on the active state; multiple statements use begin/end. Reset is ordinary code: `if (reset) goto initialise;` before the state statements. initialise can assign data and goto idle. This supersedes special reset-intrinsic and reset/initial-header proposals; no special reset priority is needed.

An executed goto schedules its destination state and immediately terminates this entire FSM invocation for the current clock edge, from any branch or nesting level. Nothing later in source order in this FSM executes, including statements outside the enclosing branch or state. Thus the first executed goto wins by stopping execution; do not lower gotos into competing independent nonblocking assignments where the last wins. When no goto executes, state holds.

On the next edge execution restarts at the top of the FSM with the destination state active. A goto does not immediately execute that state's body on the same edge. Earlier writes/transfers remain effective: goto is not rollback. Other processes/FSMs and independent queueing continue. A leading reset goto suppresses later FSM actions on reset edges. Holding reset repeats that transition; initialise's body executes after reset release. Power-up without reset remains an open decision.

The specified transfer-event control is `when (result <= total) goto idle;`. It performs the transfer and runs its body only when that transfer fires on this cycle. `if` tests computed conditions, so `if (a <= b)` must mean relational less-than-or-equal. A goto itself never waits for a transfer. Event control applies generally, not only to gotos; other when-event kinds remain unspecified.

Implementation status: the compiler currently accepts state identifier: statement, mandatory named/reset/initial FSM headers, legacy if-transfer syntax and explicit transfer plus accepts(destination,source). Existing transition-path checks do not establish whole-FSM early-exit behavior. Anonymous FSMs, level-zero ordinary statements, when migration and the new goto termination rule require implementation/verification. Migrate old transfer-if tests/examples, preserve one atomic transfer/body fire, and test source ordering, nested gotos, reset suppression and preceding transfers. Do not silently present accepts workarounds as preferred syntax. The remaining plan is what/is/the/plan.md.
