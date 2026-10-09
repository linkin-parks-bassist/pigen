---
status: green
revised_at: "2026-10-09T20:32:56+11:00"
---

`pigen_build_output_model` walks the parsed syntax tree in source child order against the completed lowering and fills the ordered output model.

Interface: `int pigen_build_output_model(pigen_output_model *model, const pigen_syntax_tree *syntax, const pigen_rtl_lowering *lowering);`
Returns 0 on success, -1 on error; a failure reports the diagnostic and stops — it may leave partial state and no test asserts a failed call left the model unchanged (how/should/compiler/builders/fail.md).

`const pigen_syntax_tree *syntax` (include/pigen/syntax.h): `pigen_syntax_node` carries `parent`/`first_child`/`last_child`/`next_sibling` (source order) and a `location.source_span`, the span an OPAQUE item holds. `PIGEN_SYNTAX_OPAQUE` nodes are the trivia/separators. Walk from the `PIGEN_SYNTAX_COMPILATION_UNIT` root down each `first_child` → `next_sibling` chain.

`const pigen_rtl_lowering *lowering` (include/pigen/rtl_lower.h), after `pigen_lower_rtl_module`: holds `semantics` + `rtl` and the memo maps. A structured syntax node maps to its semantic record (the record's `syntax` field is the `pigen_syntax_id`), then to its RTL identity through the RTL record's semantic provenance field (e.g. `pigen_rtl_object.semantic_signal`, `pigen_rtl_module.semantic_module`).

Emission rule: a `PIGEN_SYNTAX_OPAQUE` node → OPAQUE item (its `location.source_span`); a node whose kind lowers to a published RTL identity (module/object/instance/equation/process) → the matching structured item, the MODULE node opening its nested scope with its children nested inside; a structured node with no published identity is the missing-lowered-identity error (the next in-order step, not part of this walk).

Build seam: `make output-model-test` compiles `tests/output_test.c` plus the full parse+lower pipeline (src/output.c, src/rtl_lower.c, src/rtl.c, src/resolve.c, src/type_resolve.c, src/expression_analysis.c, src/expression_resolve.c, src/expression_use.c, src/predicate.c, SEMANTIC_SOURCES, SYNTAX_SOURCES); the walk test builds one small parse+lower witness. t9-skeleton pins the builder stub's signature and zero-item behavior, so it is the one existing section that calls `pigen_build_output_model` and must track the signature change; t9-nesting, t9-failed-append and t9-coverage do not call the build walk and are unchanged.

Coverage-gate mismatch (verified 2026-10-09): the hidden t9-build-walk packet (commit f656859) pins that the walked model makes `pigen_output_validate_coverage` return {1, OK}. That pin is unsatisfiable by any implementation of this rule: the gate skips structured items and requires the OPAQUE items alone to form one contiguous run to the source's first-file length, but the parser's OPAQUE nodes are token-extent spans (pigen_syntax_location_from_extent), so the whitespace between items and the trailing newline after `endmodule` are not tokens, and a structured item's own extent occupies source no OPAQUE item spans. On the packet's witness the tree yields OPAQUE [0,11) and [62,71) over 72 chars → GAP at [11,62); a no-whitespace witness still leaves the structured extent uncovered. t9-coverage reaches {1, OK} only by hand-appending OPAQUE spans covering the whole source, a layout no walk produces. Fixing it is a product decision: relax that pin to a reachable assertion, or change the gate so a structured extent needs no OPAQUE span over it. The emission rule and the rest of the walk (module nesting, identity resolution, NULL/missing-root rejection) are settled.
