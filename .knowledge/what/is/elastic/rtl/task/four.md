---
status: "unverified"
created_at: "2026-09-13T15:05:47+10:00"
scope: "local"
source: "2026-09-01 approved elastic RTL design/plan full section audit; current code baseline; David documentation retirement 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:36:20+10:00"
---

Collision-safe names. Add rtl_name.h/rtl_name.c/rtl_name_test.c and name identity. One module-local allocator assigns immutable source/internal-role names. Source requests copy provenance spelling; internal stems suffix deterministically on collision. Gate: value/value_valid/value__pigen_valid collisions, repeat stability, identical-build byte stability and invalid source span without partial names. Synthetic names may lack source span; make rtl-test rtl-name-test. This is approved future work, not implemented status.

Named files are ids.h, rtl.h, rtl_name.h, rtl_name.c, rtl_name_test.c and Makefile. Add pigen_rtl_name_id. Planned roles are SOURCE, PAYLOAD, VALID, READY, INSTANCE, TEMPORARY. pigen_rtl_assign_names(model, sources) assigns names; pigen_rtl_name_get(model, name_id) returns terminal text. Source roles copy checked identifier provenance; internal roles derive a stem and numeric suffix only on collision. Test value, value_valid, value__pigen_valid plus generated valid request, stable repeated assignment and byte-identical independently built models. Invalid source span publishes no partial names, while synthetic internal roles may lack source spans.
