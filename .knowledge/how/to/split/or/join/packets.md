---
status: "unverified"
created_at: "2026-09-13T15:03:09+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:26+10:00"
---

Use one concatenated/co-sliced transfer: `{hi, lo} <= packet;` or `packet <= {hi, lo};`. Braces are packed bit streams, leftmost most significant. Equal aggregate widths are required, not equal item counts or member widths. All members advance atomically; repeated projections consume the complete base once. Separate statements are separate consumers.
