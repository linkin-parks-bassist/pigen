---
status: "unverified"
created_at: "2026-09-13T15:05:46+10:00"
scope: "local"
source: "Codex /root document-retirement live-link check and exact owner paths 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-14T17:13:16+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:42:40+10:00"
updated_by: "opencode"
---

Task 1 is implemented in commit 30ac091 on branch elastic-rtl-task-1 (pull request 1, awaiting David's review). ids.h replaces pigen_rtl_id with distinct pigen_rtl_type_id, pigen_rtl_expr_id, pigen_rtl_object_id, pigen_rtl_instance_id, pigen_rtl_equation_id, pigen_rtl_update_id, pigen_rtl_process_id and pigen_rtl_module_id. include/pigen/rtl.h and src/rtl.c provide pigen_rtl_model with eight pointer/count/capacity arena triples; records retain only an origin span (invalid allowed for synthetic hardware) and no source-text pointer. pigen_rtl_<noun>_add appends with growth and a 2^32 count guard; pigen_rtl_<noun>_get rejects a null model, PIGEN_INVALID_ID and out-of-range indices; pigen_free_rtl_model releases every arena and zeroes the model. Task 1 introduces no enum field; the invalid-zero rule binds the kind fields later tasks add. tests/rtl_test.c covers empty-model rejection, per-kind identity, synthetic origin, identity after arena growth, and out-of-range rejection; the Makefile rtl-test target joins the test target. The gate ran the failing empty-model test before implementation; all eleven structured foundation targets then passed. Task 2 is now the next task.

Proof: (verified at 2026-09-14T19:38:05+10:00)

```bash
make rtl-test
```

Related: [what is elastic rtl task two](two.md), [what is the state](../../../the/state.md).

The implementation file set is ids.h, rtl.h, rtl.c, rtl_test.c and Makefile. Every public enum starts with invalid zero; destruction releases all arena storage and resets the entire model. The test-first gate starts with empty-model checked accessors. Read current status before treating original unchecked plan boxes as unimplemented.
