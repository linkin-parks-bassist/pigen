---
status: green
revised_at: "2026-10-03T20:48:54+10:00"
checked_at: "2026-09-30T20:31:53+10:00"
---

Task 5 owns the semantic-owner type and constant-expression adapters in include/pigen/rtl_lower.h and src/rtl_lower.c. The implementation and its tests are on master; the type/expression adapter sections of the rtl-lower-test suite are green. Failure rollback is not required: a failed lowering reports the sentinel and the compile stops (how/should/compiler/builders/fail.md).

pigen_rtl_lowering borrows the semantic and RTL models and owns identity maps keyed by source arena index. lowered_types maps data-type identities to opaque RTL type handles; lowered_expressions maps constant-expression identities to opaque RTL expression handles. Task 6 adds a third, signal-indexed lowered_endpoints map. Initialization zeroes the record and stores the borrowed model pointers; free releases all three maps and zeroes the record without freeing either model. Unpopulated slots use PIGEN_INVALID_ID.

Type lowering queries the semantic owner for state domain, signedness, packed width and ordered range shape. Concrete bounds retain evaluated values; symbolic bounds retain lowered expressions. A successful result is memoized by data-type arena identity. Invalid identities return the sentinel. Exact-integer types use pigen_data_type_exact_value and pigen_integer_unsigned_width instead of pigen_data_type_packed_width, which returns INVALID_ID for this constructor. The owner value 5 therefore gives width 3. A negative exact integer, whose unsigned width is zero, is refused.

Expression lowering obtains the owner result type and lowers children bottom-up in owner order. INTEGER, EXACT_INTEGER, BINARY, CONVERSION, SELECT and CONCATENATION retain their owner-resolved values, explicit conversions, select kind and ordered children. Constants publish PIGEN_RTL_EXPR_INTEGER with the owner value and lowered result type. EXACT_INTEGER evaluates through pigen_const_expr_evaluate_u64; it does not read the bare-integer union member. Identity memoization avoids duplicate growth for repeated source expressions. Distinct source constant identities remain distinct even when their values match. An absent expression identity or unsupported SYMBOL reports the sentinel.

tests/rtl_lower_test.c has seven families: initialization; sentinel guards; type state/width/sign/ranges; expressions and child order; constant identity/content/exact integers; failure reporting (an unbound PARAMETER symbol and an absent expression identity each lower to the sentinel, and an already-lowered valid constant still resolves through the memo); memo coherence and independent-build determinism. tests/contracts.json covers src/rtl_lower.c through make rtl-lower-test. Companion gates are make rtl-test and make rtl-name-test. The exact-integer section pins both the value-derived width and the INVALID_ID packed-width query; self-consistency between an expression's type and the lowering's own type handle alone would not catch an incorrect width.

Useful witness constraints: pigen_const_expr_intern_symbol accepts a matching PARAMETER symbol, whereas pigen_signal_add requires a SIGNAL symbol, so an unbound-parameter failure witness and a real signal must be distinct symbols. Bottom-up publication places a child before its parent, so deterministic relative offsets need not be non-negative.

Task 6 owns realization-based declaration and endpoint population. Its BOUNDARY realization is implemented; the remaining realizations still return the unimplemented sentinel. See what/is/elastic/rtl/task/six.md.
