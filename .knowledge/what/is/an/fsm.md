---
status: green
revised_at: "2026-11-11T19:57:17+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An FSM sequences a worker's actions without serializing independent channels, lowering to one synchronous controller in the shared model. It owns a state type wide enough for its declared states, unique per-state identities, resolved transition targets and an optional name (anonymous FSMs get a synthetic internal state register). Level-zero ordinary statements, ordinary reset code and state-gated statements compose in source order; `when (destination <= source)` runs its body on the transfer's fire. An executed goto schedules its destination and terminates the whole FSM invocation for that clock edge, preserving earlier writes and suppressing later statements; the next edge restarts at the top in the destination state. State actions use shared signal/expression/guard/domain/ownership services rather than private resolvers. The standalone identity record derives an initial state from the first level-zero goto, else the first declared state; startup without reset remains an open decision.
