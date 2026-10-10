---
status: green
revised_at: "2026-10-11T05:18:49+11:00"
---

`pigen_build_output_model` walks the parsed syntax tree in source child order against the completed lowering and fills the ordered output model.

Interface: `int pigen_build_output_model(pigen_output_model *model, const pigen_syntax_tree *syntax, const pigen_rtl_lowering *lowering, pigen_output_error *error);` Returns 0 on success, -1 on error; a failure reports the diagnostic and stops — it may leave partial state and no test asserts a failed call left the model unchanged (how/should/compiler/builders/fail.md).

`pigen_output_error` (include/pigen/output.h): `typedef struct { pigen_source_span span; const char *message; } pigen_output_error;` — the diagnostic reported at a node's span, following the codebase error idiom (a span + a borrowed static message, no ownership; the string outlives it). `span` is the node's `location.source_span`; `message` is static. An unset diagnostic is all-zero. The NULL-argument path (NULL model/syntax/lowering returns -1) leaves `*error` unset: it zeroes it when non-NULL and never fills it on that path. The 4th argument is accepted but otherwise unused by the walk body in this shape — the missing-identity diagnostic's span-reporting and stop-on-error wiring arrive with the implementation stage.

`int pigen_output_error_is_set(const pigen_output_error *error)`: 1 when set (non-NULL message AND `span.start <= span.end`), 0 otherwise (NULL error, NULL message, or all-invalid span). It distinguishes a NULL-argument -1 (no diagnostic) from a real missing-identity diagnostic.

`const pigen_syntax_tree *syntax` (include/pigen/syntax.h): a `pigen_syntax_node` carries `parent`/`first_child`/`last_child`/`next_sibling` (source order) and `location.source_span` (the span an OPAQUE item holds); `PIGEN_SYNTAX_OPAQUE` nodes are trivia/separators. Walk from the `PIGEN_SYNTAX_COMPILATION_UNIT` root down each `first_child` → `next_sibling` chain.

`const pigen_rtl_lowering *lowering` (include/pigen/rtl_lower.h), after `pigen_lower_rtl_module`: holds `semantics` + `rtl` + memo maps. A structured syntax node maps to its semantic record (the record whose `syntax` is the node's index), then to its RTL identity through that RTL record's provenance field (`semantic_module`, `semantic_signal`).

Emission rule: a `PIGEN_SYNTAX_OPAQUE` node → OPAQUE item (its `location.source_span`); a node lowering to a published RTL identity (module/object/instance/equation/process) → the matching structured item, the MODULE node opening its nested scope with its children nested inside; a structured node with no published identity is the missing-lowered-identity diagnostic — its public shape (the `error` seam + `pigen_output_error` + `pigen_output_error_is_set`) is established here, its span-reporting and stop-on-error wiring into the walk is the next in-order step.

Build seam: `make output-model-test` compiles `tests/output_test.c` + the full parse+lower pipeline (src/output.c, src/rtl_lower.c, src/rtl.c, src/resolve.c, src/type_resolve.c, src/expression_analysis.c, src/expression_resolve.c, src/expression_use.c, src/predicate.c, SEMANTIC_SOURCES, SYNTAX_SOURCES). t9-skeleton and t9-build-walk call `pigen_build_output_model` (4-arg) and pin the shape's ownership rule: every NULL-argument call and a clean walk leave `*error` unset (`!pigen_output_error_is_set`). t9-nesting, t9-failed-append and t9-coverage do not call the build walk and are unchanged.

Coverage gate (resolved 2026-10-10): `pigen_output_validate_coverage` is correct and unchanged; the t9-build-walk pin was relaxed because the gate skips structured items and needs the OPAQUE items alone to form one contiguous run, but the parser's OPAQUE nodes are token-extent, so whitespace and a structured item's own extent are not OPAQUE-spanned. The corrected contract asserts the walked model's OPAQUE items are source-ordered and non-overlapping (no OVERLAP/REVERSAL) while every Pigeon-construct span is a structured item the gate skips, plus the module-range prefix and OPAQUE in-bounds/non-decreasing readback.
