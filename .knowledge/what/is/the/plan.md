---
status: green
revised_at: "2026-11-11T19:56:39+11:00"
---

Prioritize implementation of settled language semantics over hardening and edge-case expansion. Keep focused behavioral tests with each implementation change; do not defer correctness of the feature being built.

The settled `when` transfer-event surface and the FSM surface (optional/anonymous names, level-zero code, ordinary reset, first-executed-goto termination) are implemented with behavioral witnesses; how/to/write/fsm/states.md owns the contract and limits.

1. Exercise the new surface in the queued-worker showcase and representative real designs: idiomatic numerical types, always, implicit transfer guards, explicit when events and ordinary reset code. Keep compatibility witnesses separate from authored Pigen showcases (where/are/behavioral/examples.md).
2. Continue substantive specification coverage: arrays/generate endpoint scopes, remaining expression/connection forms, mixed-case selector scheduling and qualified cases, sequential case/loop lowering, impure function/task side effects and nonblocking write reconstruction. Work requirement-by-requirement against what/is/the/spec.md and the compatibility contract; do not shrink requirements to supported syntax.

After the language work above, return to the hardening backlog: nested/partial-region/spanning-producer and multidimensional alias mappings; multiword constant/power evaluation; standalone negative exact types and untyped negative bitwise typing; unresolved bounds; alias writer and cross-clock audits; specialization diagnostic provenance; and broader compatibility/edge-case matrices. Existing owners retain exact capability boundaries: how/to/connect/child/ports.md, what/is/elastic/rtl/task/five.md, what/is/broken.md and how/to/run/repository/verification.md. Preserve interrupted work rather than discarding or quietly expanding it ahead of the priorities above.

Open decisions: startup without an asserted FSM reset; explicit-valid gate versus replacement for computed automatic inputs; pipeline yield surface; fabric manifest/SVG retention; struct-like child instantiation. Reset intrinsic proposals and last-goto-wins semantics are superseded.

Deferred to Pigen 1.0: ready-cycle validation (elastic RTL Task 7) and route negative witnesses. Stages diagnose and stop without rollback.
