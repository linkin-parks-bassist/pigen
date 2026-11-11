---
status: green
revised_at: "2026-11-11T18:54:02+11:00"
---

Prioritize implementation of settled language semantics over hardening and edge-case expansion. Keep focused behavioral tests with each implementation change; do not defer correctness of the feature being built.

1. Implement `when (destination <= source)` as transfer-fire event control. `if` must test computed conditions, including ordinary <= comparisons. Preserve one atomic transfer/body fire, migrate legacy transfer-if syntax/tests, and replace accepts workarounds in showcases. The event surface is specified but not implemented.
2. Implement optional FSM names, anonymous `fsm @(posedge clk)`, and ordinary level-zero statements alongside state-gated statements. Replace mandatory reset/initial header machinery with ordinary `if (reset) goto initialise;` and an ordinary initialise state.
3. Implement the settled goto rule: the first executed goto schedules its destination and terminates the entire FSM invocation for this edge, from any nesting depth. Suppress every later FSM action, preserve earlier effects, and restart at the top next edge with the destination state. Test nested branches, level-zero reset suppression, transfers before/after goto and independence of other processes. These tests establish the language rule, not optional hardening. FSM/event ownership: how/to/write/fsm/states.md.
4. Exercise the new surface in the queued-worker showcase and representative real designs: idiomatic numerical types, always, implicit transfer guards, explicit when events and ordinary reset code. Keep compatibility witnesses separate from authored Pigen showcases (where/are/behavioral/examples.md).
5. Continue substantive specification coverage: arrays/generate endpoint scopes, remaining expression/connection forms, mixed-case selector scheduling and qualified cases, sequential case/loop lowering, impure function/task side effects and nonblocking write reconstruction. Work requirement-by-requirement against what/is/the/spec.md and the compatibility contract; do not shrink requirements to supported syntax.

After the language work above, return to the hardening backlog: nested/partial-region/spanning-producer and multidimensional alias mappings; multiword constant/power evaluation; standalone negative exact types and untyped negative bitwise typing; unresolved bounds; alias writer and cross-clock audits; specialization diagnostic provenance; and broader compatibility/edge-case matrices. Existing owners retain exact capability boundaries: how/to/connect/child/ports.md, what/is/elastic/rtl/task/five.md, what/is/broken.md and how/to/run/repository/verification.md. Preserve interrupted work rather than discarding or quietly expanding it ahead of the priorities above.

Open decisions: startup without an asserted FSM reset; explicit-valid gate versus replacement for computed automatic inputs; pipeline yield surface; fabric manifest/SVG retention; struct-like child instantiation. Do not let unrelated open choices block when/FSM work with explicit reset witnesses. Reset intrinsic proposals and last-goto-wins semantics are superseded.

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Stages diagnose and stop without rollback.
