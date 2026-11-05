---
status: green
revised_at: "2026-11-06T01:08:33+11:00"
---

The accepted hidden line-4 FSM emitted-SV test `test_fsm_guard_gated_single_destination` (packet fsm-emitted-sv-line-4-guard-gated-single-destination, commit 99d2f451) is UNSATISFIABLE by any pigen emission. Its `lhs_identifier` (tests/compile_test.c) returns a pointer INTO the middle of the emitted SV buffer (the start of the LHS register-name token before ` <= `), and the false-branch reads are raw pointer arithmetic `idle_next + strlen(" <= start ? 1 : ")` — also mid-buffer. But `register_name_equals` walks identifier characters until a non-identifier char, then returns `*a=='\0' && *b=='\0'`. A mid-buffer identifier is never NUL-terminated: it is always followed by a non-identifier char (a space before `<=` on an LHS line, or `;` on a false-branch read). So `register_name_equals` returns 0 for ANY register name of ANY length, and `assert(register_name_equals(idle_dest, run_dest))` (line 525 of the packet file; line 947 of the gate's merged file) fails for every possible output.

Verified at candidate 909248a (work/fsm-production-implementation HEAD): the emitted SV is the correct single-destination controller —
  logic [1:0] w;
  assign done = w == 1 ? 1 : 0;
  always @(posedge clk)
    w <= start ? 1 : w;
    w <= !start ? 0 : w;
    w <= rst ? 0 : w;
The register is named `w` because the witness FSM is named after its module (`fsm w ...`), so the name is a single character; but the failure is not the name length — probes confirm `register_name_equals` returns 0 for mid-buffer identifiers of 1..4 chars and 1 only for an identifier at the very end of the buffer. The production code (src/fsm_lower.c + src/compile.c + src/output.c) is correct; the defect is the test helper.

Fix (test-only, cointos_correct_tests on the line-4 packet): make the comparison operate on NUL-terminated copies of the located tokens (e.g. copy the located identifier into a fixed buffer, or compare by bounded length), so the single-destination law is witnessed without requiring the register name to end the SV string. Do not change the production emission to chase the broken helper.
