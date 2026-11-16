---
status: green
revised_at: "2026-11-16T14:05:39+11:00"
---

Call pigen_data_type_packed_width(model, type) from include/pigen/data_type.h to obtain the canonical constant-expression identity for a packed width. Void returns zero width and has no RTL object representation (how/are/void/data/types/represented.md). Use pigen_const_expr_evaluate_u64 only when a concrete width is required; failure means the expression was not established as a representable concrete width. Semantic type and constant-expression queries preserve symbolic identity instead of inspecting rendered ranges. Source evidence: include/pigen/data_type.h, include/pigen/semantic.h and the width queries already used in tests/semantic_test.c. Concatenation and multidimensional width algebra are owned by how/are/concatenation/types/represented.md.
