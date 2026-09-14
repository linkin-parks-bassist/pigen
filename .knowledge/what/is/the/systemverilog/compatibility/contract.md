---
status: "unverified"
created_at: "2026-09-13T15:03:22+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:33+10:00"
---

Pure SV must retain ports/values/widths/signedness/events/reset/observable cycles. Pigen elsewhere cannot reinterpret unrelated SV. If meaning cannot be preserved, diagnose at source; deliberate subset restrictions must be specified and diagnosed. Textual identity is unnecessary. The explicit exception is colonless declaration-count notation; colon-bearing ranges and expression selects retain SV meaning.
