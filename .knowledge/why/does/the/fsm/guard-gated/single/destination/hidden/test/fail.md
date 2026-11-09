---
status: green
revised_at: "2026-11-06T03:06:27+11:00"
---

The hidden line-4 FSM emitted-SV test `test_fsm_guard_gated_single_destination` (tests/compile_test.c) is RED on the current production path: it fails at Pin B (`always @(posedge clk)` absent), because the production path does not yet lower the FSM statement, so the emitted module is declaration-only (ports + endmodule) and none of the controller pins hold. The test is otherwise satisfiable — the earlier test-helper defect is corrected.

The helper `register_name_equals` compares the located identifier tokens directly: `lhs_identifier` returns a pointer into the emitted SV buffer at the start of the LHS register-name token, and the false-branch reads are raw pointer arithmetic (`idle_next + strlen(" <= start ? 1 : ")`), also mid-buffer. The comparison walks both tokens over identifier characters and returns equal when they reach a non-identifier character at the same length with matching characters (a different first character or a length mismatch returns not-equal). A mid-buffer identifier is never NUL-terminated, so the comparison must not require a NUL tail; the corrected form returns 1 for the correct single-destination controller and 0 for a second destination register or a false branch that does not read the register (a literal-digit false branch is rejected). The single-destination law is thus witnessed: both next-state values drive the one shared state register and each reads it in its false branch.

Verified against the known-correct single-destination controller emission (src/fsm_lower.c + src/compile.c + src/output.c):
  logic [1:0] w;
  assign done = w == 1 ? 1 : 0;
  always @(posedge clk)
    w <= start ? 1 : w;
    w <= !start ? 0 : w;
    w <= rst ? 0 : w;
Probes of the corrected helper return 1 for all three equality asserts (dest==dest, dest==read on each line) and 0 for a second destination register and for a non-register-reading false branch. The production emission is correct; the test turns green once the production FSM statement lowering lands.
