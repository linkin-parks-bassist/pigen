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

Ordered output model. Create output.h/output.c/output_test.c. Layout-only items are opaque source spans or matching structured module/object/instance/equation/process IDs. Walk syntax child order, preserve trivia/separators as opaque spans, require every structured node to have lowered identity. Gate: exact monotonic coverage; reject gaps/overlaps/reversal/wrong-source/invalid refs; failed nested append restores layout/item/child counts and destroy zeroes model; make output-model-test rtl-test. This is approved future work, not implemented status.

pigen_output_model and pigen_build_output_model consume source spans, syntax child order, lowering mappings and RTL IDs. Planned kinds OPAQUE, MODULE, RTL_OBJECT, RTL_INSTANCE, RTL_EQUATION, RTL_PROCESS. Opaque records hold only spans, structured records only matching identities; module records hold nested layout IDs. No API returns/searches opaque bytes. Syntax declarations/processes use recorded mappings; punctuation/trivia between extents become uninterpreted spans. Missing lowered identity diagnoses the original structured syntax span. Nested failure restores layout/item/child counts; destructor zeroes model.
