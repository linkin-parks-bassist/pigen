---
status: "unverified"
created_at: "2026-09-13T15:03:16+10:00"
scope: "local"
source: "agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:20+10:00"
---

One expression traversal retains each symbol occurrence with projection, context and evaluation predicate, plus one deduplicated summary per signal. Ternary arms use opposite condition polarities; impossible paths add no uses. Lvalue projection, runtime index and type bounds have distinct contexts. A signal read and written keeps all context bits in one summary while occurrences stay distinct.
