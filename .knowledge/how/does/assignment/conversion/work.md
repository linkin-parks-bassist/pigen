---
status: green
revised_at: "2026-11-16T01:03:24+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Resolve the destination separately, resolve the RHS with its applicable context, then ask the destination data-type family for a final conversion. Pigen numerical expressions retain their intrinsic promotion and intermediate values; same-family narrowing keeps low bits and widening is exact. Native SV assignments use destination width plus self/common operand contexts through pigen_resolve_assignment_value (how/are/expressions/resolved.md). Named record patterns receive target schemas (how/to/construct/typed/records.md). Family/raw-vector changes require explicit casts. Transfer construction requires exact final type and shape. Compound updates explicitly convert back to the variable type (how/are/combinational/compound/updates/represented.md).

The sequence is destination lvalue/type/shape resolution, contextual RHS resolution, destination-family assignment conversion, final nonidentity conversion and checked transfer construction. int<->uint and numerical<->raw use type'(expression); C-style casts reject. pigen_resolve_assignment_value takes syntax/model/scope/expression/target/policy/error. Direct transfer constructors independently reject mismatches before storage allocation. Conversion metadata may carry scaling/rounding policy without changing generic topology.

RHS construction can relocate expression and lvalue arenas. resolve_when_statement reacquires destination expression/lvalue pointers by stable ID after resolving the value, before comparing shapes. Ordinary transfers likewise reacquire the destination before slice-width checks. tests/cores/when_value* checks 1,536 ternary/logical/relational transfers, stalls and exact same-fire body counts; its ternary exercises arena growth under native assignment contextualization.

The buffered-slice guard respects a root explicit cast, including grouped casts; it must not strip a requested resize and compare the original slice width. Implicit projection-width diagnostics remain enforced. tests/native_protocol_test.py checks an explicitly widened computed-record bit through 256 stalled transfers.

Every assignment conversion is legal as an explicit cast, including raw-vector resize and ordinary SV integral casts. SV size casts share pigen_resolve_cast_type between analysis/direct resolution and preserve signs/state (how/are/systemverilog/size/casts/resolved.md). data_type.c delegates that fallback to assignment policy. expression-resolve-test checks int[17] resize, implicit unsigned rejection, explicit unsigned and bit[8] literal casts; continuous-intrinsics checks bit[4]'(peek(source)).

Four-state-to-two-state assignments/casts carry FOUR_TO_TWO, clearing X/Z before subsequent use (how/are/four-state/values/converted/to/two-state/types.md). Emission preserves widths/signs for INTEGER_RESIZE and other conversions; a sign-only cast cannot resize. Composite children retain their intrinsic widths before outer resizing; operands pad to recorded working widths. tests/cores/integer_resize* independently checks all 65,536 inputs through signed/unsigned resizing, arithmetic and concatenation under stalls.

Concatenated destinations convert the whole RHS before splitting writes, in ordinary transfers and when events. Placeholder inference determines each child type and destination width rather than borrowing source width. tests/cores/concat_resize* checks 8,192 byte results across truncation, extension, placeholders and independently stalled outputs. Child-lvalue resolution snapshots child IDs before allocation.
