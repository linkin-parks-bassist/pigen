---
status: "unverified"
created_at: "2026-09-13T15:04:56+10:00"
scope: "local"
source: "intrinsic expression design Range-derived numerical results; src/data_type.c around line 1792"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:27+10:00"
---

Source-visible Pigen numerical division/remainder remain fail-closed until runtime divide-by-zero behavior is specified. Ordinary SV division/remainder retain ordinary SV rules. src/data_type.c explicitly rejects DIVIDE/MODULO in concrete Pigen integer numerical resolution; operator syntax support alone does not mean the Pigen operation is admissible.
