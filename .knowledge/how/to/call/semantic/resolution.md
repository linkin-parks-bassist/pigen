---
status: green
revised_at: "2026-11-15T09:42:46+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

pigen_resolve_semantics consumes a syntax tree, destination semantic model, positive pigen_resolve_policy maximum_generated_bits and optional semantic error. pigen_resolve_expression resolves values, pigen_resolve_constant_expression uses the same walker but requires a constant identity with explicit literal domain, and pigen_resolve_assignment_value accepts a target data type for the final boundary conversion. These are library APIs, not production CLI options.

The caller releases the semantic model with pigen_free_semantic_model on success or failure. Resolution can leave a partial model on error; its arenas remain owned by that model. src/semantic.c frees the owned arenas, including validity-action records, and clears the structure. Borrowed syntax/source storage is released by its own owners after semantic users finish.

src/resolve.c separately owns temporary pipeline/reset binding tables. pigen_resolve_semantics frees those tables through one exit path on success and failure. Unconsumed pipeline_reset bindings diagnose a missing pipeline in the same process. tests/generate_semantic_test.c exercises repeated generated pipelines, reset actions and model destruction; its sanitizer gate checks these ownership paths.
