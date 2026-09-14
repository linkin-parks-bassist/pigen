---
status: "unverified"
created_at: "2026-09-13T15:03:12+10:00"
scope: "local"
source: "SPEC.md; agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:30+10:00"
---

An FSM owns a state type, one initial state, unique state identities and resolved transition targets. It lowers to one synchronous controller reset to its header initial state. State actions use shared signal/expression/guard/domain/ownership services rather than private resolvers.
