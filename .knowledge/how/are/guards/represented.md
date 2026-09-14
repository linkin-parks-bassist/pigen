---
status: "unverified"
created_at: "2026-09-13T15:03:11+10:00"
scope: "local"
source: "agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:15+10:00"
---

Predicates are canonical conjunctions of typed (expression identity, polarity) atoms with interned true/false. Order is irrelevant, repeats idempotent, opposite polarity makes false. Two predicates are proven exclusive when an atom has opposite polarity or one is false. Complete control nesting and dangling-else association survive; guards never compare rendered strings.
