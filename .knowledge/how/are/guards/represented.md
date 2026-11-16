---
status: green
revised_at: "2026-11-16T17:37:42+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Predicates are canonical conjunctions of typed (expression identity, polarity) atoms with interned true/false. Order is irrelevant, repeats idempotent, and opposite polarity makes false. Complete control nesting and dangling-else association survive; guards never compare rendered strings.

src/predicate.c:pigen_predicates_mutually_exclusive first checks opposite atoms or an impossible predicate. Its bounded condition_implied helper can additionally prove that one guard implies the opposite of a compound atom in another guard. Sufficient implications follow logical AND/OR/NOT, groups, evaluated-value result operands and literal integers zero/one. Comparisons and conversions remain opaque. Four-state truth is preserved: a negated condition requires known false, and positive requires known true; unknowns are not silently Booleanized. The helper is read-only and has a 1024-visit budget per candidate implication. Failure to prove exclusion remains conservative.

This lets ordinary buffered ownership checks recognize a previous transfer's guard inside an accumulated clocked-loop exit condition, without exempting loops from ownership validation (how/are/clocked/foreach/loops/represented.md). Native direct/child/routed loop-exit profiles pass with this implementation (/tmp/pigen-clocked-exits-focused4.log). tests/predicate_test.c passes 3,136 guard-pair soundness checks over all four-state input pairs, including opaque case equality, and a positive accumulated-exit proof (/tmp/pigen-clocked-exits-predicate.log). Native loop-exit and shared foreach regressions also pass with compiler ASan/UBSan (/tmp/pigen-clocked-exits-current-asan.log). The full clocked-case/foreach-exit checkpoint passes (/tmp/pigen-clocked-exits-verify.log).
