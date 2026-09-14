---
status: "unverified"
created_at: "2026-09-13T15:04:53+10:00"
scope: "local"
source: "include/pigen/semantic_error.h; elastic RTL plan Task 11"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:33+10:00"
---

pigen_semantic_error contains origin identity, physical source span and borrowed message pointer. Preserve both origin and strongest available span through phase errors. The planned compile error adapter must not discard macro-origin or source-location information; it is not implemented yet.
