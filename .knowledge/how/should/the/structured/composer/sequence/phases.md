---
status: "unverified"
created_at: "2026-09-13T15:03:19+10:00"
scope: "local"
source: "docs/superpowers/plans/2026-09-01-elastic-rtl-vertical-slice.md Task 11"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:22+10:00"
---

The planned pigen_compile_source invokes preprocess, parse syntax, resolve semantics, validate ready dependencies, lower RTL module, assign RTL names, build output model and emit SV in that order. Preserve native origin/span in compile errors, free completed models in reverse order and never call prototype lex/rewrite APIs. This API is planned, not present.
