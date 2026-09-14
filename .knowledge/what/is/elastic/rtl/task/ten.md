---
status: "unverified"
created_at: "2026-09-13T15:05:51+10:00"
scope: "local"
source: "2026-09-01 approved elastic RTL design/plan full section audit; current code baseline; David documentation retirement 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:36:21+10:00"
---

Terminal SV emitter. Create sv_emit.h/sv_emit.c/sv_emit_test.c. Precedence-aware rendering uses RTL kind/operator/type records and final names. Copy bounded opaque spans exactly in output order; never tokenize/search or query semantic owners. Gate: mixed golden output with signed conversion/concat/primitive/guard; malformed coverage/unnamed refs/bad spans leave no partial output; no strstr/strchr/symbol/data-type/transfer-type lookup matches; make sv-emit-test output-model-test rtl-test. This is approved future work, not implemented status.

pigen_emit_systemverilog(sources, layout, rtl, output_string) consumes validated coverage and named RTL. Use one fixed precedence per expression kind, structurally necessary parentheses and one private operator rendering table; render conversions from RTL types only. The golden test contains leading/trailing opaque comments, signed conversion, concat, primitive parameter, equation and guarded update. Malformed coverage, unnamed objects, invalid children and out-of-bounds spans leave no partial output. Structural search for strstr, strchr, pigen_symbol_lookup, pigen_data_type_ or pigen_transfer_type_ in sv_emit.c must have no matches. Files sv_emit.h/c, sv_emit_test.c, Makefile.
