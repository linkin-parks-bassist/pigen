---
status: green
revised_at: "2026-10-16T08:57:26+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

pigen_compile_source (include/pigen/compile.h, src/compile.c) invokes, in order: add the source to the manager, preprocess, lex, parse syntax, resolve semantics, lower the RTL module (declarations then transfers), assign RTL names, build the output model, and emit SV. It preserves native origin/span in compile errors and frees completed models in reverse order. The structured path lexes the preprocessed text itself rather than calling prototype lex/rewrite APIs. This API is present and implemented (Task 11, quarantined compiler composition).
