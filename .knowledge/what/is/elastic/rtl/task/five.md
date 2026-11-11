---
status: green
revised_at: "2026-11-11T17:14:25+11:00"
checked_at: "2026-09-30T20:31:53+10:00"
---

Task 5 owns semantic type and constant-expression adapters in include/pigen/rtl_lower.h and src/rtl_lower.c. Failure returns the invalid sentinel and compilation stops; rollback is not required (how/should/compiler/builders/fail.md).

pigen_rtl_lowering borrows semantic/RTL models and owns source-identity maps: lowered_types, lowered_expressions and Task 6's signal-indexed lowered_endpoints. Initialization stores borrowed pointers; free releases the maps and zeroes the lowering without freeing either model. Unpopulated slots use PIGEN_INVALID_ID.

Type lowering obtains state domain, signedness, packed width and ordered range shape from the semantic owner. Concrete bounds use pigen_const_expr_evaluate_i64, preserving negative coordinates; unresolved bounds retain lowered expressions. Successful types are memoized by data-type identity. Exact-integer types use pigen_data_type_exact_value and pigen_integer_unsigned_width because packed_width is invalid for that constructor. Positive 5 has width 3; standalone negative exact-integer types remain unsupported.

Constant-expression lowering obtains the result type and lowers children bottom-up in owner order. INTEGER, EXACT_INTEGER, BINARY, CONVERSION, SELECT and CONCATENATION retain resolved values, conversions, selection kind and ordered children. Exact constants use pigen_const_expr_evaluate_u64 rather than reading the ordinary-integer union field. Identity memoization prevents repeated growth; distinct constant identities remain distinct even when values match. Missing expressions and unsupported symbols fail.

Semantic index/select lowering uses lower_coordinate for negative constant operands. It evaluates the mathematical coordinate with the signed evaluator and publishes a signed concrete RTL integer with sign/magnitude, without asking the standalone exact-integer adapter to create a negative hardware type. Nonnegative or unevaluated coordinates follow ordinary expression lowering. Range widths and endpoint overlap/equality checks share signed coordinate evaluation; count rules remain separate (how/to/normalize/declaration/counts.md).

The signed evaluator first reuses existing nonnegative evaluation, then handles negative exact/packed literals, parameter references, conversions, unary +/−/~ /!, and checked addition/subtraction/multiplication/division/remainder. Resolved fixed widths and signedness are applied. Unknown, unsupported or overflowing values fail rather than invent coordinates; general operator/symbolic-bound coverage remains unfinished.

rtl-lower-test covers initialization, sentinel guards, state/width/sign/ranges, expression ordering, constant identity/exact integers, failure reporting, memo coherence and independent-build determinism. Unbound-parameter witnesses differ from real SIGNAL symbols. Tests constrain reported failures and memo stability, not rollback. semantic-test covers signed coordinates, INT64_MIN and range-width overflow; child-port-endpoint-test simulates negative range/indexed aliases under stalls. Task 6 owns realization-based declarations/endpoints; its storage and boundary tests share tests/rtl_lower_test.c. Companion gates: rtl-test and rtl-name-test.
