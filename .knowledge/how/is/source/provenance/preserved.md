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

Sources have stable typed identities. Syntax uses half-open expanded-token extents, origin chains and optional contiguous physical spans. Semantic owners retain strongest available provenance; cross-file/synthetic owners never fabricate ranges. Diagnostics follow invocation origins primarily and actual/definition origins for spelling/explanation.
