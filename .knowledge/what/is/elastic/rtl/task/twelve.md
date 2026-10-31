---
status: green
revised_at: "2026-10-31T10:59:01+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Verification and durable boundary. TASK 12 IS COMPLETE.

Completion evidence, checked on current master:
- The standalone structured C/backend targets are green: source-test, preprocess-test, transfer-type-test, syntax-model-test, integer-test, semantic-test, predicate-test, expression-resolve-test, expression-use-test, resolve-test, rtl-test, rtl-name-test, rtl-lower-test, output-model-test and sv-emit-test each exit 0.
- No quarantine violations: `grep -nE 'strstr|strchr|pigen_symbol_lookup|pigen_data_type_|pigen_transfer_type_' src/sv_emit.c` has no matches; `strstr|marker|rewrite|generated.*lookup` has no matches in src/rtl.c, src/rtl_lower.c, src/output.c or src/compile.c. src/ready_graph.c no longer exists on master.
- No concrete source transfer enums in lowering: rtl_lower.c references only backend-neutral realization kinds.

Current boundary: `vertical-slice-test` (and the other corpus .pigen targets) is red on master: its input examples/buf_pipeline.pigen uses the prototype dialect the production structured frontend diagnoses by design ("data type must precede the transfer type"), so the target fails at the pigen step as per-subsystem migration debt (what/is/broken.md). It passed at Task 12 completion, when the production entry point still accepted that dialect; the CLI wiring to the structured frontend later made corpus targets red. The "12 ordered packets" simulation claim dates to that earlier entry point and is not reproducible on the current master. Production integration, full core, pipelines, FSMs and fabrics remain pending until their own gates pass.
