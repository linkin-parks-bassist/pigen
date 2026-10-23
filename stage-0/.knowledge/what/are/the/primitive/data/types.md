---
status: green
revised_at: "2026-09-13T15:07:28+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Pigen primitives are two-state signed `int[n]`, unsigned `uint[n]`, and neutral `bit`/`bit[n]`. Use `bit[8]` for neutral eight-bit storage. Ordinary SV `byte` remains its distinct signed eight-bit type in the SV spelling domain; unsupported byte declarations stay opaque, never silently become bit[8]. Later scalar, aggregate, enum and user-defined types are deferred.
