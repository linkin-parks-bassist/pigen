---
status: green
revised_at: "2026-10-16T10:39:10+11:00"
---

The terminal SV emission for per-member co-slice register/logic assignments lives in src/assignments.c, in pigen_emit_assignment_routes: the loop that emits each R/L/M member as `dest <= <rewritten expression>`, where the RHS goes through pigen_emit_rewritten_expression(output, assignment->expression, ..., primitives).

pigen_emit_rewritten_expression is declared in include/pigen/assignments.h and called from two files: four internal sites inside src/assignments.c (width checks, conditional payload casts, and the per-member RHS) and ten call sites in src/pigen.c (guard rewrites, destination prefixes, and the procedural register-member emission). Changing its signature therefore forces edits in include/pigen/assignments.h and every src/pigen.c call site, not just src/assignments.c. A behavior change confined to the co-slice RHS must instead be made inside src/assignments.c (e.g. a file-local helper plus a local re-emit at the per-member RHS site) so it does not ripple through the shared signature.
