---
status: "unverified"
created_at: "2026-09-13T15:03:07+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:28+10:00"
---

Pigen primitives are two-state signed `int[n]`, unsigned `uint[n]`, and neutral `bit`/`bit[n]`. Use `bit[8]` for neutral eight-bit storage. Ordinary SV `byte` remains its distinct signed eight-bit type in the SV spelling domain; unsupported byte declarations stay opaque, never silently become bit[8]. Later scalar, aggregate, enum and user-defined types are deferred.
