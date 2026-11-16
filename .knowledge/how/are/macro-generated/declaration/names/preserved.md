---
status: green
revised_at: "2026-11-16T11:27:03+11:00"
---

src/resolve.c:declaration_name_span materializes expanded declaration names into model-owned `<pigen-macro>` sources whenever token provenance is non-source, or expanded spelling differs from its origin spelling. Ordinary source names keep their source spans. Typedef declarations, including native endpoint aliases, use this helper alongside signals/genvars and other existing callers. Byte-based lookup still finds the name; declaration spans continue to enforce source-point eligibility.

This avoids rejecting a valid declaration whose name originated in a macro definition outside the invocation's source span. semantic.c:macro_name_allowed accepts contained names or the explicitly materialized macro-name source, without permitting arbitrary unrelated source names.

Owned macro replacements consume the invocation backtick. syntax.c:add_opaque excludes that byte from the preceding opaque gap when a non-source token's expansion begins at a root-source macro name. Otherwise global expansion can leave a stray backtick immediately before emitted `typedef` text. Original opaque content remains byte-preserved outside owned replacements.

For owned typedef width/range expressions, output.c:package_expression_text serializes the expression's exact expanded token extent when macro provenance is present. Several declarations or subexpressions may share one invocation source span; selecting tokens by that span can incorrectly insert an entire macro body into a single width. Non-macro expressions retain their source text.

Evidence: tests/native_protocol_test.py:data_typedef_emission_tests includes six all-family direct/child/fabric macro profiles. One macro emits two distinct record layouts and a native endpoint alias; typed signed field checks from both layouts are compared with independent physical pin bits under stalls/reset, with warnings-fatal SV lint and simulation. The profiles pass with the production compiler, ASan/UBSan and the full repository verification gate. Schema identity uses expanded extents too (how/are/anonymous/packed/record/schemas/represented.md). Owned typedef emission is described in how/are/data/typedefs/emitted.md.
