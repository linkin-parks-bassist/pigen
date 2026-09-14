---
status: "unverified"
created_at: "2026-09-13T15:03:11+10:00"
scope: "local"
source: "SPEC.md; agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:30+10:00"
---

A domain is canonical by resolved clock declaration identity and event edge. Each clocked process retains separate syntax identity, clock-expression occurrence, module and provenance even if it shares that domain. Pigen actions require a synchronous one-edge always/always_ff; first Pigen use binds a signal and later uses must match.
