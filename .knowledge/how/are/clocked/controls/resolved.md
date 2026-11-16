---
status: green
revised_at: "2026-11-16T17:37:42+11:00"
---

Clocked if resolves its condition with pigen_resolve_control_expression, sharing the self-determined SV context used by combinational controls (how/are/combinational/controls/resolved.md). Native expressions retain SV sizing/sign/cast boundaries; concrete Pigen numerical subtrees retain their arithmetic. Integral vector truth uses reduction OR and exact integers use logical normalization. Branch predicates use truth === 1: known nonzero enters then, while zero or X/Z-only truth enters else, matching native SV. Buffered condition reads join normal transfer dependencies; peek remains observational (how/does/an/atomic/transfer/fire.md).

src/resolve.c:resolve_when_statement resolves one normal header transfer, then uses a known fire-entry decision for its body guard. Unknown fire does not enter the body or execute a loop exit. Leaf effects additionally honor earlier FSM gotos and enclosing clocked-loop exits. Qualified controls retain the original scalar four-state truth for native checks and capture liveness on entry (how/are/qualified/if/statements/represented.md; how/are/clocked/foreach/loops/represented.md).

tests/clocked_guard_test.py checks thirteen existing smoke fixtures against native if/else decisions. Directed source-valid/readiness injection checks generated destination validity, source consumption, reset/else payload selection and validity actions over 128 decisions per ordinary fixture. Two when fixtures additionally check body writes over 256 decisions each, including X/Z validity. These are controlled guard/handshake probes; independent storage and native channel queue tests establish the broader transport behavior. Current smoke passes (/tmp/pigen-clocked-exits-smoke.log). Clocked-case native-SV oracles separately cover vector truth and unbased control fills (how/are/clocked/case/statements/represented.md).

The smoke script retains interface/storage/diagnostic/lint checks and uses behavioral probes rather than obsolete exact strings for normalized guards. The full clocked-case/foreach-exit checkpoint including these guard probes passes (/tmp/pigen-clocked-exits-verify.log).
