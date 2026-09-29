---
status: green
revised_at: "2026-09-29T16:50:53+10:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Collision-safe names. Add rtl_name.h/rtl_name.c/rtl_name_test.c and name identity. One module-local allocator assigns immutable source/internal-role names. Source requests copy provenance spelling; internal stems suffix deterministically on collision. Gate: value/value_valid/value__pigen_valid collisions, repeat stability, identical-build byte stability and invalid source span without partial names. Synthetic names may lack source span; make rtl-test rtl-name-test.

Named files are ids.h, rtl.h, rtl_name.h, rtl_name.c, rtl_name_test.c and Makefile. Add pigen_rtl_name_id. Planned roles are SOURCE, PAYLOAD, VALID, READY, INSTANCE, TEMPORARY. pigen_rtl_assign_names(model, sources) assigns names; pigen_rtl_name_get(model, name_id) returns terminal text. Source roles copy checked identifier provenance; internal roles derive a stem and numeric suffix only on collision. Test value, value_valid, value__pigen_valid plus generated valid request, stable repeated assignment and byte-identical independently built models. Invalid source span publishes no partial names, while synthetic internal roles may lack source spans.

The name-identity shape is declared and landed by the skeleton (elastic-rtl-task-4-skeleton): pigen_rtl_name_id, the zero-invalid pigen_rtl_name_kind (SOURCE/PAYLOAD/VALID/READY/INSTANCE/TEMPORARY), the pigen_rtl_name record (origin span, kind, model-owned terminal text), the pigen_rtl_name_request shape, the names/name_count/name_capacity arena on pigen_rtl_model, and pigen_rtl_assign_names/pigen_rtl_name_get stubs (invalid id / NULL) in rtl_name.h/rtl_name.c, plus the rtl-name-test target and its contracts.json entry. make rtl-name-test is green. The collision-suffixing, span-to-text copying and get-text behavior remain unimplemented and are owned by the test-contract and implementation stages.
