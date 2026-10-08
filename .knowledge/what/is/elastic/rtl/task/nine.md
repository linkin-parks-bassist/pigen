---
status: green
revised_at: "2026-10-08T16:56:41+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Ordered output model. Create output.h/output.c/output_test.c. Layout-only items are opaque source spans or matching structured module/object/instance/equation/process IDs. Walk syntax child order, preserve trivia/separators as opaque spans, require every structured node to have lowered identity. Gate: exact monotonic coverage; reject gaps/overlaps/reversal/wrong-source/invalid refs; failed nested append restores layout/item/child counts and destroy zeroes model; make output-model-test, with rtl-test remaining green. This is approved future work; nothing is implemented yet.

pigen_output_model and pigen_build_output_model consume source spans, syntax child order, lowering mappings and RTL IDs. Planned kinds OPAQUE, MODULE, RTL_OBJECT, RTL_INSTANCE, RTL_EQUATION, RTL_PROCESS. Opaque records hold only spans, structured records only matching identities; module records hold nested layout IDs. No API returns or searches opaque bytes. Syntax declarations/processes use recorded mappings; punctuation/trivia between extents become uninterpreted spans. Missing lowered identity diagnoses the original structured syntax span. Nested failure restores layout/item/child counts; destructor zeroes model.

Shape and next step: the first bounded slice is the skeleton — the output-item and output-model structures, the six planned kinds, the public signatures, ownership and error contracts, minimal stubs, and the make output-model-test build seam (its own tests/contracts.json entry covering src/output.c + src/source.c + src/util.c). The pigen_rtl_module owner ranges (objects, instances, equations, processes) are the structured identities the output layout references. It lands green as one self-contained t9-skeleton section (empty model builds to zero items; append one opaque item and one structured item and read them back; a wrong-kind query returns PIGEN_INVALID_ID; a freed model zeroes to NULL/0; the build seam passes).

Remaining frontier, in order: (1) append and child/nesting ownership; (2) exact monotonic-coverage gate rejecting gaps, overlaps, reversal, wrong-source and invalid refs; (3) failed nested append restoring layout/item/child counts; (4) pigen_build_output_model walking syntax child order against the lowering mappings; (5) the missing-lowered-identity diagnostic at the original syntax span. The destructor zeroing the model and the build seam are already part of the skeleton.
