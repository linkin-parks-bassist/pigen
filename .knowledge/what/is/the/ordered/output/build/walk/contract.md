---
status: green
revised_at: "2026-10-09T06:37:13+11:00"
---

`pigen_build_output_model` walks the parsed syntax tree in source child order against the completed lowering and fills the ordered output model.

Current signature (a stub returning 0 with zero items) takes only `pigen_output_model *model`; the consuming interfaces are not yet in the header and must be fixed before the walk can be written.

Elaborated interface (the contract the skeleton pins):
`int pigen_build_output_model(pigen_output_model *model, const pigen_syntax_tree *syntax, const pigen_rtl_lowering *lowering);`
Returns 0 on success, -1 on error; a failure reports the diagnostic and stops — it may leave partial state and no test asserts a failed call left the model unchanged (how/should/compiler/builders/fail.md).

Consuming interfaces, all already on master:
- `const pigen_syntax_tree *syntax` (include/pigen/syntax.h): the forest to walk. `pigen_syntax_node` carries `parent`/`first_child`/`last_child`/`next_sibling` (child/sibling order is the source order) and a `location` whose `source_span` is the `pigen_source_span` an OPAQUE item holds. `PIGEN_SYNTAX_OPAQUE` nodes are the trivia/separators: they become OPAQUE items with that span. Walk from the `PIGEN_SYNTAX_COMPILATION_UNIT` root down each `first_child` → `next_sibling` chain.
- `const pigen_rtl_lowering *lowering` (include/pigen/rtl_lower.h), after `pigen_lower_rtl_module` has run: it holds `semantics` + `rtl` and the memo maps (lowered_types/expressions/endpoints) — the semantic-to-RTL "lowering mappings". A structured syntax node maps to its semantic record (the semantic record's `syntax` field is the `pigen_syntax_id`), then to its RTL identity through the RTL record's semantic provenance field (e.g. `pigen_rtl_object.semantic_signal`, `pigen_rtl_module.semantic_module`).

Emission rule (the walk's contract): a `PIGEN_SYNTAX_OPAQUE` node → OPAQUE item (its `location.source_span`); a node whose kind lowers to a published RTL identity (module/object/instance/equation/process) → the matching structured item, with the MODULE node opening its nested scope and its children nested inside; a structured node with no published lowered identity is a missing-lowered-identity error (the next in-order step, not part of this walk).

Build seam consequence: `make output-model-test` currently compiles `tests/output_test.c src/output.c src/source.c src/util.c`; the new signature pulls in `src/syntax.c`, `src/semantic.c` and the RTL/lowering sources, and the test must build one small parse+lower witness. The existing t9-skeleton/nesting/failed-append/coverage sections are unchanged (they do not call the build walk).
