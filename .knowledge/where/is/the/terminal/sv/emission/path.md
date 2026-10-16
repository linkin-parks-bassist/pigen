---
status: green
revised_at: "2026-10-16T12:55:21+11:00"
---

Terminal SV emission for a transfer spans two sites that must not be conflated.

The per-member co-slice register/logic assignment loop lives in src/assignments.c, pigen_emit_assignment_routes: for each R/L/M member it emits `dest <= <rewritten expression>`, where the RHS goes through pigen_emit_rewritten_expression(output, assignment->expression, ..., primitives). That loop only handles uppercase destination codes: `if (assignment->destination_code != 'R' && != 'L' && != 'M') continue;`. A destination registered with a lowercase code is skipped there.

The separate manual write clause — the `previous <= state` shape, where the RHS is a bare same-fire register read — is NOT handled by that loop. It is emitted by the extract_manual_signal_write path in src/pigen.c, which appends the RHS RAW via pigen_append_range (no rewriter) and registers the destination with lowercase code 'm'. The co-slice `previous <= state` X-propagation defect lives at this pigen.c emission site, not in src/assignments.c: because the manual-write destination is 'm' and the RHS is emitted raw, no change confined to src/assignments.c can alter that emission.

pigen_emit_rewritten_expression is declared in include/pigen/assignments.h and called from both files (internal sites in src/assignments.c and call sites in src/pigen.c: guard rewrites, destination prefixes, and the procedural register-member emission), so changing its signature ripples across both files plus the header. A behavior change to the per-member R/L/M RHS can be kept file-local in src/assignments.c (a file-local helper re-emitting at that RHS site); a behavior change to the manual-write path must be made in src/pigen.c.
