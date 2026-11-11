---
status: green
revised_at: "2026-11-11T18:30:22+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The grammar is `state identifier : statement`. One statement follows the colon directly; multiple use ordinary begin/end. Actions run only when the state is active. goto is terminal on its syntactic path, changes state only under its explicit enclosing guard, and otherwise state holds. Overlapping transitions require structural exclusion.

The specified event-control surface is `when (result <= total) goto idle;`: perform the nonblocking transfer and execute the body only if it fires on this cycle. `if` tests a computed condition; ordinary `if (a <= b)` must mean relational less-than-or-equal. This separates a transfer event from a Boolean calculation instead of overloading the same if syntax. The design applies to transfer-event control generally, not only FSM gotos; other when event kinds have not been specified.

Implementation remains pending. The current compiler implements legacy if-transfer syntax and explicit transfer plus accepts(destination,source); these are implementation status, not the preferred target syntax. Implement when parsing/semantic lowering, preserve one atomic fire for the transfer and event body, migrate old transfer-if examples/tests, and restore ordinary comparison parsing in if. Do not silently substitute accepts workarounds in showcases or infer that a goto waits for a transfer.
