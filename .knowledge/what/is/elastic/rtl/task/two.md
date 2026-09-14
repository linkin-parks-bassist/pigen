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

Canonical RTL types and expressions. Add interned RTL types, immutable typed/spanned expression nodes and ordered child arena in rtl.h/rtl.c/rtl_test.c. Nodes cover integer/bits/object/unary/binary/conditional/conversion/index/select/concat. Store resolved operation/conversion records, not text. Gate: interning identity, child order, all node kinds/provenance, and invalid identity rejection with unchanged expression/child counts; make rtl-test. This is approved future work, not implemented status.

Approved Task 2 touches only include/pigen/rtl.h, src/rtl.c and tests/rtl_test.c. Consumes Task 1 identities and resolved operation/conversion records. APIs include pigen_rtl_type_intern(model, type), pigen_rtl_expr_add_concatenation(model, type, children, count, span), and pigen_rtl_expr_children(model, expr). Intern identical types to one identity; preserve ordered children exactly. Public kind enum begins PIGEN_RTL_EXPR_INVALID then INTEGER, BITS, OBJECT, UNARY, BINARY, CONDITIONAL, CONVERSION, INDEX, SELECT, CONCATENATION. Every node stores kind, type and provenance span; sequence children belong to one append-only arena. Types carry emitted packed layout, signedness, state domain and structural symbolic bounds/width expressions. Nodes store resolved operation/conversion records, never operator text. Validate types, operands, objects, selectors and every child before append. Invalid references must leave both expression and child counts unchanged. Exercise every kind, literal values, object references, explicit conversions, provenance and ordered concatenations. Work test-first: observe the intended failing new API test, implement, run make rtl-test, git diff --check, inspect status, stage only the three named files and make a coherent green-task commit. Do not attach this to production.
