---
status: green
revised_at: "2026-11-16T20:00:48+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

A compilation layout interleaves opaque immutable written spans and structured module/package layouts. Nested layouts order opaque items and RTL identity references. Coverage must be exact, monotonic and once per written byte, rejecting gaps/overlaps/reversed/wrong-source spans. Terminal emission copies opaque bytes exactly and renders prepared items; layout is not semantics.

At a structured package boundary, syntax.c:pigen_parse_syntax emits root filler only up to the package's first byte, then sets opaque_byte_cursor to that byte before parse_structured_package builds its children. The package's first child owns its wrapper. Leaving the cursor at the previous structured item would emit intervening ordinary packages twice. retained_namespace_tests in tests/native_protocol_test.py includes ordinary packages between semantic packages and runs warnings-fatal lint on the output.

RETAINED items have prepared text for semantically bound native payload observations (how/are/retained/native/axi/observations/represented.md) and canonical callees requiring qualified names; ordinary surrounding bytes remain intact, while macro-bearing retained items render their own expanded token extents with a trailing separator. Several routines from one macro invocation can share a written span; selecting by that span would duplicate bodies and concatenate routine boundaries. tests/native_function_effect_test.py covers qualified/imported calls to multiple macro-defined routines (/tmp/pigen-retained-effects-expanded5.log). Canonical routine imports may also be prepared after the package header (how/are/retained/package/calls/bound.md). These are output-model preparation operations; OPAQUE spans retain their byte-copy contract and terminal emission performs no name binding.

Related: when/can/a/module/become/structured.md; what/is/the/ordered/output/build/walk/contract.md.
