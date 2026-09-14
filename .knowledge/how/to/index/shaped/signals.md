---
status: "unverified"
created_at: "2026-09-13T15:03:08+10:00"
scope: "local"
source: "agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:25+10:00"
---

Indexing a non-scalar expression removes its leading unpacked shape dimension and preserves data type. Only after shape becomes scalar does indexing select packed data. Selectors must be scalar-shaped. Unpacked slicing and concatenation reject until they have explicit structural semantics; never flatten shape or recover it from rendered brackets.
