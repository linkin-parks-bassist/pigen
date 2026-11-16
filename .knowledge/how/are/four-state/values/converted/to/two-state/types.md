---
status: green
revised_at: "2026-11-16T01:16:48+11:00"
---

Actual assignment and explicit-cast conversions from four-state integral values to two-state integral types select PIGEN_CONVERSION_FOUR_TO_TWO. Every X/Z bit becomes zero before surrounding arithmetic, comparisons or later casts use the value. Conversion legality and existing family/raw-vector restrictions remain governed by data_type.c; operand working-view conversions do not implicitly clear unknowns.

operation.h declares the conversion kind. data_type.c validates source FOUR and target TWO domains and selects it after assignment/explicit policy succeeds. Semantic constant bit evaluation clears unknown bits for a two-state target. semantic.c and rtl.c accept the typed conversion record through their conversion constructors.

rtl_lower.c folds constant FOUR_TO_TWO values whose source contains X/Z into typed literal bits, using pigen_const_expr_evaluate_bit at arbitrary widths. Known-value constant conversions retain their source identities. This keeps cleared Z constants out of downstream array writes. Runtime conversions remain explicit.

sv_emit.c renders the finished conversion as high-to-low chunks of at most 64 bits. It resizes the intrinsic operand to the target width before shifting each chunk into position, clears unknowns with a native two-state int or longint cast, truncates that chunk, concatenates and applies target signedness. An explicit working-width cast prevents native cast context from producing width warnings. Source signedness governs extension before clearing. Procedural reads remain at the conversion's evaluation point; there are no continuous aliases or added objects. A conversion repeats its operand ceil(width/64) times instead of once per bit, avoiding the large nested expansion exposed by foreach integer arithmetic.

Evidence: tests/four_to_two_chunk_test.py compares signed, unsigned and nested conversions at fourteen widths spanning 1 through 257 against native SV typedef casts over 256 X/Z patterns, includes constant all-Z conversions at every width, and requires fatal lint. tests/foreach_test.py checks ordered/static-local traversal against native SV and native AXI observations. tests/cores/continuous_intrinsics.pigen state_casts uses independent native bit-variable references for widening, scalar conversion, clearing before XOR/equality and 128-bit X/Z extension. case_comb checks numerical/bit casts of unknown logical results. how/does/assignment/conversion/work.md owns assignment policy; how/are/procedural/case/operands/resolved.md owns case comparison context.
