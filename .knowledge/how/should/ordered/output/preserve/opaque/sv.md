---
status: "unverified"
created_at: "2026-09-13T15:03:18+10:00"
scope: "local"
source: "docs/superpowers/specs/2026-09-01-elastic-rtl-vertical-slice-design.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:21+10:00"
---

A compilation layout interleaves opaque immutable written spans and structured module layouts. Nested module layouts order opaque items and RTL identity references. Coverage must be exact, monotonic and once per written byte, rejecting gaps/overlaps/reversed/wrong-source spans. Terminal emission copies opaque bytes exactly and renders structured items; layout is not semantics.

Related: [when can a module become structured](../../../../../../when/can/a/module/become/structured.md).
