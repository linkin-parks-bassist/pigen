---
status: "unverified"
created_at: "2026-09-13T15:03:17+10:00"
scope: "local"
source: "agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:18+10:00"
---

Conditional compilation uses the source-order macro environment before syntax. Inactive branches emit no expanded tokens and have no preprocessing effects. Nested selection crosses textual includes with shared macro state. Include operands expand privately and must become exactly one quoted/angle path; operand tokens never enter syntax, and include edges retain written directive/operand spans and provider-selected source identity.
