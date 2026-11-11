---
status: green
revised_at: "2026-11-11T18:43:10+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The currently implemented state grammar is `state identifier : statement`. One statement follows the colon directly; multiple use ordinary begin/end. Actions run only when the state is active. goto is terminal on its syntactic path, changes state only under its explicit enclosing guard, and otherwise state holds. Overlapping transitions require structural exclusion.

The specified event-control surface is `when (result <= total) goto idle;`: perform the nonblocking transfer and execute the body only if it fires on this cycle. `if` tests a computed condition; ordinary `if (a <= b)` must mean relational less-than-or-equal. This separates a transfer event from a Boolean calculation instead of overloading the same if syntax. The design applies to transfer-event control generally, not only FSM gotos; other when event kinds have not been specified.

Implementation remains pending. The current compiler implements legacy if-transfer syntax and explicit transfer plus accepts(destination,source); these are implementation status, not the preferred target syntax. Implement when parsing/semantic lowering, preserve one atomic fire for the transfer and event body, migrate old transfer-if examples/tests, and restore ordinary comparison parsing in if. Do not silently substitute accepts workarounds in showcases or infer that a goto waits for a transfer.

Requested FSM surface: names are optional; the anonymous form is `fsm @(posedge clk) begin ... end`. Ordinary statements may live directly in the FSM body at level zero; they execute independently of the active named state. `state name: statement` adds state gating, not a requirement that every statement belong to a state. Reset is ordinary level-zero code such as `if (reset) goto initialise;`, and initialise is an ordinary state that can assign data and goto idle. This replaces the proposed special reset intrinsic and mandatory reset/initial header clauses. No special reset intrinsic is requested by this design.

This surface is specified but not implemented. Open semantics: competing level-zero/state transitions on one edge, whether a level-zero goto terminates the entire FSM's execution for that edge, interaction with state-local transfers, and power-up without reset. Clock-edge state transition timing must be explicit: do not assume a goto immediately executes the destination state's body on the same edge. The assistant recommendation is edge-terminating goto with destination-state work on a following edge; that priority/timing rule is not yet a user decision. Do not invent special reset priority that is absent from the ordinary control-flow rules.

FSM states sequence a worker's actions; they do not serialize every surrounding channel. Independent FIFO ingress and buffered output permit requests to queue while the worker calculates, and permit a completed answer to wait while the next job runs. Backpressure eventually stalls publishing and then upstream traffic when storage fills. The queued-job simulation demonstrates this separation; see where/are/behavioral/examples.md.
