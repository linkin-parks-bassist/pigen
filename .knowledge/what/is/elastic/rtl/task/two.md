---
status: green
revised_at: "2026-11-10T06:06:20+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

TASK 2 IS COMPLETE. Canonical interned RTL types and immutable typed/spanned expression nodes live in include/pigen/rtl.h and src/rtl.c, gated by tests/rtl_test.c. Nodes cover integer, bits, object, unary, binary, conditional, conversion, index, select and concatenation. Each stores its resolved operation/conversion record, never operator text, plus kind, type and provenance span; sequence children belong to one ordered append-only arena.

Types carry emitted packed layout, signedness, state domain and structural symbolic bounds/width expressions. Identical types intern to one identity. Ordered children are preserved exactly. Literal nodes own least-significant-word-first value/X/Z records and a bit count: integers use sign and magnitude, bits disallow a negative sign, and integer/two-state literals reject X/Z; overlapping masks, nonzero value under masks and nonzero padding bits are rejected. Dimensions and literals are copied into model ownership and freed with the model; uint64_t constructors are convenience wrappers.

Every append validates types, operands, objects, selectors and all children before publishing, and an invalid reference leaves expression and child counts unchanged. Accessor pointers expire on arena growth while IDs stay stable. expr_append copies child slices before resizing either arena, including slices returned by pigen_rtl_expr_children, and checks allocation-size limits.

Evidence: rtl-test passes and the structured foundation targets pass; regression coverage includes forced growth, ascending/descending dimensions, symbolic identity, invalid-bound rollback, 130-bit signed integers, four-state bits, copied literal ownership and invalid masks; AddressSanitizer/UndefinedBehaviorSanitizer runs are clean. The task touches only include/pigen/rtl.h, src/rtl.c and tests/rtl_test.c and is not attached to production.
