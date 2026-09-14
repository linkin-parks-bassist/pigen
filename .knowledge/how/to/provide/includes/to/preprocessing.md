---
status: "unverified"
created_at: "2026-09-13T15:04:50+10:00"
scope: "local"
source: "include/pigen/preprocess.h; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:26+10:00"
---

Provide pigen_source_provider with context and pigen_include_loader callback. Loader receives including source identity and path bytes/length and returns selected source identity or error message. Source manager owns actual file bytes; preprocessing represents the written include edge and expanded meaning without inserting private include-operand tokens into syntax.
