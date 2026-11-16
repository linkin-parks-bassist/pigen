---
status: green
revised_at: "2026-11-16T16:52:47+11:00"
---

Typed unique/unique0/priority if chains work in ordered combinational and clocked transfer processes, including native channels, FSMs and guards around pipelines. syntax.h retains qualifier and authored else_if relationships; semantic/RTL procedural records carry both. Constructors, process attachment and emission validate placement and chain structure.

Combinational lowering emits complete ordered bodies, preserving authored else-if chains and explicit else begin if nesting. Native SV supplies overlap/completeness checks and branch-entry evaluation even when a body changes a local selector.

Clocked lowering preserves independent transfer fire/offer predicates. A check-only semantic tree mirrors enclosing blocks, if/else and when events. rtl_lower.c attaches its lowered tree to the clocked process; sv_emit.c emits it on the bound edge before nonblocking updates. Checks are not continuous or masked by channel readiness. Scalar four-state truth supplies checks; case-equality branch decisions make transfer guards choose else for unknown conditions. Base control typing, unknown branch decisions and when entry are owned by how/are/clocked/controls/resolved.md. This path has no impure function evaluation or blocking clocked writes.

FSM state check trees are gated by their active-state conditions. Each qualified chain captures the accumulated termination condition before resolving its own branches. Earlier executed gotos suppress its check; a goto in its selected arm preserves that chain's entry check. Nested checks after the goto are suppressed. This follows the first-goto contract in how/to/write/fsm/states.md.

Qualified guards around pipelines retain complete enclosing conditions for pipeline transfers and reset bindings. Stage-local grammar remains declarations and assignments for all transfer types (how/are/pipeline/stage/local/declarations/parsed.md); how/are/generated/pipelines/resolved.md owns lexical identity and reset binding.

tests/native_protocol_test.py covers eighteen buffered/native declarations, three clocked and six combinational native-SV oracles, and 27 clocked transport profiles across all families/qualifiers, storage and direct/routed child/fabric paths. These pass normal and compiler ASan/UBSan checks (/tmp/pigen-qualified-if-transport-test.log; /tmp/pigen-qualified-if-comb-test.log; /tmp/pigen-qualified-if-impl-asan.log). Nine generated combinational array/child profiles also pass normal/sanitizer runs (/tmp/pigen-qualified-if-arrays-final.log; /tmp/pigen-qualified-if-arrays-asan.log).

qualified_if_fsm_oracles passes independent native-SV first-goto/reset/inactive-state/check parity; qualified_if_fsm_tests passes 27 native transport profiles (/tmp/pigen-qualified-fsm-focused.log). tests/native_array_test.py:qualified_pipeline_tests passes nine explicit/implicit/parameterized-child profiles across all families with qualified reset/pipeline guards (/tmp/pigen-qualified-pipelines.log). All 27 FSM transport profiles, three FSM oracles and nine pipeline profiles also pass compiler ASan/UBSan (/tmp/pigen-qualified-fsm-pipeline-asan.log). Eighteen explicit/implicit generated-FSM qualifier profiles also pass compiler ASan/UBSan and assertion-enabled standard-pin simulation (/tmp/pigen-qualified-generated-fsm-asan-final.log); their state branches are complete if/else chains.

Failed buffered/structured clocked parsing diagnoses instead of losing processes; retained SV uses complete statement extents. test_process_rollback verifies failure and arena rollback. The hardening full gate passes (/tmp/pigen-qualified-if-guard-verify2.log). The pre-FSM qualifier full gate passes (/tmp/pigen-qualified-if-impl-verify.log). Full verification of the FSM/pipeline extension passes (/tmp/pigen-qualified-fsm-pipeline-verify.log). Invalid metadata/no-partial-output emitter tests pass.

Retained function/task effects remain integration work (how/are/scoped/combinational/processes/represented.md). Typed clocked cases now share the check-tree mechanism; their extension and validation are owned by how/are/clocked/case/statements/represented.md.
