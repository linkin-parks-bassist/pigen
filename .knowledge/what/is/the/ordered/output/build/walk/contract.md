---
status: green
revised_at: "2026-10-11T03:14:32+11:00"
---

`pigen_build_output_model` walks the parsed syntax tree in source child order against the completed lowering and fills the ordered output model.

Interface: `int pigen_build_output_model(pigen_output_model *model, const pigen_syntax_tree *syntax, const pigen_rtl_lowering *lowering, pigen_output_error *error);` Returns 0 on success, -1 on error; a failure reports the diagnostic and stops — it may leave partial state and no test asserts a failed call left the model unchanged (how/should/compiler/builders/fail.md). The 4th argument `error` receives the diagnostic when one is reported and may be NULL (the walk still returns -1).

`pigen_output_error { pigen_source_span span; const char *message; }` carries the diagnostic (span the node's location.source_span, message a borrowed static string); `pigen_output_error_is_set` returns 1 when it is set, 0 otherwise (the test's way to tell a NULL-argument -1 from a real diagnostic).

`const pigen_syntax_tree *syntax` (include/pigen/syntax.h): `pigen_syntax_node` carries `parent`/`first_child`/`last_child`/`next_sibling` (source order) and a `location.source_span`, the span an OPAQUE item holds; `PIGEN_SYNTAX_OPAQUE` nodes are the trivia/separators. Walk from the `PIGEN_SYNTAX_COMPILATION_UNIT` root down each `first_child` → `next_sibling` chain.

`const pigen_rtl_lowering *lowering` (include/pigen/rtl_lower.h), after `pigen_lower_rtl_module`: holds `semantics` + `rtl` and the memo maps. A structured syntax node maps to its semantic record (the record's `syntax` field is the `pigen_syntax_id`), then to its RTL identity through the RTL record's semantic provenance field (e.g. `pigen_rtl_object.semantic_signal`, `pigen_rtl_module.semantic_module`).

Emission rule: a `PIGEN_SYNTAX_OPAQUE` node → OPAQUE item (its `location.source_span`); a node whose kind lowers to a published RTL identity → the matching structured item, the MODULE node opening its nested scope with its children nested inside. A required node (module, or signal declarator) with no published identity is reported at its span by the missing-lowered-identity diagnostic (what is the missing lowered identity diagnostic of the build walk) — the walk's next in-order step.

Build seam: `make output-model-test` compiles `tests/output_test.c` plus the full parse+lower pipeline (src/output.c, src/rtl_lower.c, src/rtl.c, src/resolve.c, src/type_resolve.c, src/expression_analysis.c, src/expression_resolve.c, src/expression_use.c, src/predicate.c, SEMANTIC_SOURCES, SYNTAX_SOURCES); the walk test builds one small parse+lower witness. t9-skeleton is the one existing section that calls `pigen_build_output_model`; it pins the walk's error contract (a NULL model/syntax/lowering returns -1, error unset) and tracks the 4-argument signature. t9-nesting, t9-failed-append and t9-coverage do not call the build walk.

Coverage-gate mismatch (resolved): the original t9-build-walk packet pinned that the walked model makes `pigen_output_validate_coverage` return {1, OK}; that pin is unsatisfiable by this rule because the gate skips structured items and requires the OPAQUE items alone to form one contiguous run to the first file's length, but the parser's OPAQUE nodes are token-extent spans so whitespace, the trailing newline and each structured item's own extent are uncovered by any OPAQUE item. The product decision is resolved (relax the pin): the gate is correct and unchanged; the t9-build-walk contract instead asserts the walked OPAQUE items are source-ordered and non-overlapping (no OVERLAP/REVERSAL), the module range a non-empty prefix, and the OPAQUE spans in-bounds/non-decreasing.

Revision: 1389b77159fae7525f039222342c38e8ea9a18934d73aa99bf2274f7029a5579
