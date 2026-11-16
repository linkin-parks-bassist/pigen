---
status: green
revised_at: "2026-11-16T17:37:41+11:00"
---

Clocked case/casez/casex use a typed resolver under integration validation. src/syntax.c:parse_procedural_case supplies the shared item/label tree. src/resolve.c:resolve_case_operands establishes maximum-width/common-signedness context for selector and labels. Comma labels share one body and item identity. Pure transfer cases use this path; existing unqualified function/transfer compositions retain a selective specialize.c decoder fallback. Qualified effectful mixed forms remain separate work.

resolve_clocked_case builds known Boolean matches. Ordinary case uses case equality. Wildcard modes compare bits with case equality, permitting Z on either side for casez and X/Z for casex; conjunction is balanced. Guards combine each match with prior matches negated. Default negates every item match regardless of authored position, making ownership exclusion explicit. Bodies resolve in source order with normal transfer/fire semantics.

Clocked buffered selectors and labels may consume tokens, unlike combinational observations requiring peek. analyze_transfer includes guard reads and deduplicates them with payload reads (how/does/an/atomic/transfer/fire.md). An evaluated-value true guard retains the selector read even in a default-only case whose payload ignores it (how/are/evaluated/values/represented.md).

Qualified cases and nested qualified controls retain a check-only native tree on the bound clock, preserving enclosing if/else/when and authored defaults. FSM entry termination is captured before the case's own branches: earlier gotos suppress checks, while a selected arm's goto preserves its entered check. Independent reference construction is owned by how/are/fsm/case/entry/checks/verified.md. Arbitrary effectful function/task cases retain separate restrictions.

Enclosing clocked controls preserve native vector and unknown branch entry (how/are/clocked/controls/resolved.md).

Evidence: tests/clocked_case_test.py covers twelve native-SV selection profiles at widths 4/33/65/129, dynamic/comma labels, X/Z, default-first and vector conditions; three qualification oracles; four control-fill profiles; three FSM entry oracles; 54 observing-selector and 18 consuming-selector all-family native storage/child/fabric profiles; buffered validity actions; and a 33-token default-only constant-output/backpressure oracle. These pass with compiler ASan/UBSan (/tmp/pigen-clocked-case-sized-asan.log). Independent buffered selector, label and payload inputs also pass normal and sanitizer checks for all three case kinds: 97 atomic tokens, duplicate labels, 65-bit X/Z and independent stalls (/tmp/pigen-clocked-case-independent.log; /tmp/pigen-clocked-case-independent-asan.log). Seven legacy mixed-function fixtures pass (/tmp/pigen-clocked-case-existing.log).

Explicit evaluated-value operand sizing passes fatal lint, emitter and runtime zero-field regressions. Behavioral smoke probes replace obsolete exact guard strings and pass normal/sanitizer checks (how/are/clocked/controls/resolved.md). Full verification of the clocked-case/foreach-exit checkpoint passes (/tmp/pigen-clocked-exits-verify.log). Pipeline-specific liveness/reset extensions have their own validation boundary (how/are/generated/pipelines/resolved.md).
