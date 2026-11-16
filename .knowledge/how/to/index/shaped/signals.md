---
status: green
revised_at: "2026-11-16T03:03:25+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Indexing a non-scalar expression removes its leading unpacked shape dimension and preserves data type. Only after shape becomes scalar does indexing select packed data. Selectors must be scalar-shaped. pigen_shape_dimension_bounds supplies checked signed bounds and cardinality in declaration order; counts use descending N-1:0 bounds. pigen_shape_element copies dimension suffixes before interning can grow the arena. pigen_expr_add_index caches unpacked rank before that call. tests/semantic_test.c forces suffix-arena growth and checks nested aggregate shapes. Unpacked slicing and concatenation reject until they have explicit structural semantics; never flatten shape or recover it from rendered brackets.

Static declaration emission preserves signed explicit unpacked bounds and count direction separately from the packed data type. Failed shape lowering rejects instead of silently emitting a scalar. how/are/unpacked/signal/declarations/emitted.md owns the lowering and negative-bound child-output witnesses.
