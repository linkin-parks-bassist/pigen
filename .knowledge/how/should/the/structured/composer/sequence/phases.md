---
status: green
revised_at: "2026-10-04T00:24:56+10:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The planned pigen_compile_source invokes preprocess, parse syntax, resolve semantics, lower RTL module, assign RTL names, build output model and emit SV in that order. Preserve native origin/span in compile errors, free completed models in reverse order and never call prototype lex/rewrite APIs. This API is planned, not present.
