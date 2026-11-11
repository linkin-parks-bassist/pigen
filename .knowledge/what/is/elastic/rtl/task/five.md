---
status: green
revised_at: "2026-11-11T17:40:50+11:00"
checked_at: "2026-09-30T20:31:53+10:00"
---

Task 5 owns semantic type and constant-expression adapters in include/pigen/rtl_lower.h and src/rtl_lower.c. Failure returns the invalid sentinel and compilation stops; rollback is not required (how/should/compiler/builders/fail.md).

pigen_rtl_lowering borrows semantic/RTL models and owns source-identity maps: lowered_types, lowered_expressions and Task 6's signal-indexed lowered_endpoints. Initialization stores borrowed pointers; free releases maps and zeroes lowering without freeing models. Unpopulated slots use PIGEN_INVALID_ID.

Type lowering obtains state domain, signedness, packed width and ordered ranges from the semantic owner. Concrete bounds use pigen_const_expr_evaluate_i64, preserving negative coordinates; unresolved bounds retain expressions. Successful types memoize by identity. Exact-integer types use pigen_data_type_exact_value and pigen_integer_unsigned_width because packed_width is invalid for that constructor. Positive 5 has width 3; standalone negative exact-integer types remain unsupported.

Constant-expression lowering obtains the result type and lowers children bottom-up. INTEGER, EXACT_INTEGER, BITS, BINARY, CONVERSION, SELECT and CONCATENATION preserve their owner records. BITS allocates ceil(state_count/64) zeroed RTL words and copies every normalized value/X/Z bit before the constructor copies that storage; partial final words retain only valid bits. This prevents host-word truncation and out-of-bounds reads. Positive EXACT_INTEGER constants pack all canonical 32-bit magnitude limbs into zeroed 64-bit RTL words, without a host-width limit. Negative exact hardware types remain unsupported. Identity memoization prevents repeated growth; distinct identities remain distinct even when values match. Missing expressions and unsupported symbols fail.

Semantic index/select lowering uses lower_coordinate for negative constants: evaluate the mathematical coordinate and publish a signed concrete RTL integer with sign/magnitude, without constructing a standalone negative exact hardware type. Other coordinates follow ordinary lowering. Range widths and endpoint overlap/equality share signed evaluation; count rules remain separate (how/to/normalize/declaration/counts.md).

The signed evaluator reuses nonnegative evaluation, then handles negative exact/packed literals, parameters, conversions, unary +/−/~/!, checked arithmetic, comparisons, logical/bitwise operators and conditionals. Conditions select only the needed branch; logical short-circuiting skips unnecessary operands. Resolved fixed widths/signedness apply. Unknown, unsupported or overflowing values fail. Fixed-width shifts/reductions support known operands up to 64 bits: logical right zero-fills, signed arithmetic right sign-fills, left shifts truncate, and reductions inspect declared width. Out-of-width shifts saturate appropriately.

The u64 query rejects left shifts overflowing its capacity when the declared result is wider, and rejects wide XNOR results whose high ones cannot fit. Runtime RTL retains those operations. Multiword constant evaluation, signed power and general symbolic bounds remain unfinished. Source bitwise witnesses use explicitly sized signed operands; untyped negative bitwise expressions need type-resolution work.

rtl-lower-test covers identity, type/expression correctness, failure, memo stability and deterministic builds. semantic-test covers signed coordinates, conditional/short-circuit evaluation, shifts/reductions, INT64_MIN, range overflow and refusal to truncate wide results. exact-wide-test simulates a 194-bit decimal exact literal against an independent hexadecimal reference. wide-literal-test simulates 193-bit four-state data and a signed 128-bit arithmetic shift. child-port-endpoint-test covers coordinate aliases under stalls. Task 6 owns realization-based declarations/endpoints; companion gates are rtl-test and rtl-name-test.
