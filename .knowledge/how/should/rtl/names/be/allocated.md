---
status: "unverified"
created_at: "2026-09-13T15:03:17+10:00"
scope: "local"
source: "docs/superpowers/specs/2026-09-01-elastic-rtl-vertical-slice-design.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:21+10:00"
---

Names are requests, never identities. One module-local allocator turns source-derived stems/internal roles into deterministic collision-safe final names before emission. Mapping stays in RTL IR; later code never looks up meaning by generated text. Colliding source names must not alter semantic references.
