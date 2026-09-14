---
status: "unverified"
created_at: "2026-09-13T15:04:56+10:00"
scope: "local"
source: "include/pigen/data_type.h; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:16+10:00"
---

A canonical alias stores typedef symbol ID and resolved target type ID separately. Width/projection/state/operation queries follow stored target without another symbol lookup. Capability checks are alias-transparent while exact alias identity can survive preserving operations. Source-order lexical eligibility/shadow barriers are syntax facts, distinct from canonical semantic targets.
