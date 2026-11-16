---
status: green
revised_at: "2026-11-16T14:05:38+11:00"
---

Constant-only analysis in the CASE literal domain normalizes each appended analyzed node's cached constant recipe. src/expression_analysis.c:normalize_analyzed_constant calls constant_self/constant_context, then restores the node's original type with constant_convert. This interns types/constants only, publishing no semantic expressions/lvalues/children/constraints. The expression arena stays fixed; the constant arena may move. constant_coerce snapshots its source type before width construction. The failing input and all 36 foreign-IP boundary profiles plus twelve negatives pass ASan/UBSan (/tmp/pigen-extern-boundaries-asan-fixed.log). The full repository gate passes (/tmp/pigen-extern-boundary-final-verify.log).

Native integral recipes propagate self/common width and signedness through unary/binary operations and conditional values. Comparisons use their own common operand context; logical operands, reductions, shift/power counts and concatenation children size themselves. Unbased literals expand at the owning width. Casts preserve boundaries and state conversion. Aliases and packed dimensions are restored in cached constants before subsequent selection type construction, so fill-derived bounds determine the correct width. Recursion is limited to 128 levels with checked allocation arithmetic.

Concrete Pigen numerical arithmetic retains its existing recipes and promotion decisions. Exact integers retain mathematical values until explicit conversion; they are not forced through a nonexistent packed self-width. Numerical casts containing fills analyze their operands in CASE context; decimal-only nonfill numerical casts retain Pigen evaluation. For example, uint[64]'(2147483647+1) remains the lossless value 2147483648.

Packed replication uses temporary concatenation recipes. Constant-only analysis supports zero-count fields alongside positive-sized fields after validating every ignored constant operand; runtime analyzed and direct paths preserve ignored evaluations/dependencies through evaluated-value records. The operator contract and tests belong to how/are/packed/replications/analyzed.md. Runtime direct concatenation/replication children use the same local filled-value context as call arguments.

Constant-only `$bits` and fixed-array bound/size/rank queries inspect expression/type arguments without publishing operand reads and produce signed 32-bit SV integer constants. Contracts: how/are/fixed-size/bit/inquiries/resolved.md and how/are/fixed/array/inquiries/resolved.md.

Typed peek/valid/ready/accepts records support inquiry operands without publishing runtime reads; only eligible peek arguments cache constants. Runtime controls retain invalid recipes (how/are/handshake/calls/analyzed.md).

Analyzed $signed/$unsigned calls become typed CAST records with unchanged width/state and requested signedness. Constant $clog2 reads the locally normalized argument. Unsupported/runtime/unknown coordinate values still reject through signed-coordinate evaluation. Structural array selectors use this pass via array_selection_constant (how/are/native/axi/collection/selections/resolved.md).

The publishing pigen_resolve_constant_expression CASE route applies matching semantic self-context after materialization. Its materializer normalizes only selection operands whose source syntax contains fills, restoring their original types, and leaves exact integer operands untouched. Plain mutable procedural operands retain identity: unnecessary conversion projections would move their reads outside ordered always_comb and can create combinational loops. This keeps pure analyzed and published constant values equal without using semantic materialization inside analysis.

pigen_expression_contains_unbased_fill in expression_analysis.h/expression_analysis.c is the shared syntax-discovery owner for analysis and runtime contextual routing. It visits groups, unary/binary nodes, casts, selections, conditional operands, concatenation/replication bodies and call arguments.

Evidence: tests/expression_resolve_test.c compares pure analyzed recipes, published CASE constants and runtime values against literal expectations, while checking expression counts, children, constraints and arena identity. Coverage includes fills, signedness/state, unknown conditions, native/Pigen cast boundaries, packed selections and signed coordinate limits. tests/generate_semantic_test.c checks scalar/view endpoint identity and no expression publication. Eighteen filled-coordinate and eighteen replicated-coordinate tests/native_array_test.py cases score coordinates through all three AXI families and child/fabric/slice contexts under stalls/reset.
