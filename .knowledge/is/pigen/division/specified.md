---
status: green
revised_at: "2026-11-15T15:14:58+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Source-visible Pigen numerical division/remainder remain fail-closed until runtime divide-by-zero behavior is specified. src/data_type.c rejects DIVIDE/MODULO whenever either operand is a concrete Pigen int[N]/uint[N], including aliases and mixed raw-SV operands. Compound /= and %= do not bypass this rule. Exact compile-time constants and ordinary SV integral division/remainder keep their own laws.

For two raw SV integral operands, resolve_sv_binary_operation uses the maximum operand width and a signed result only when both operands are signed. Each operand is reinterpreted at its original width before padding, so a negative narrow operand is zero-extended when the common operation is unsigned. Unchanged operand types and same-type results preserve their identities without unnecessary conversions. The result retains four-state capability if either operand is four-state. No lossless Pigen promotion supplies the raw SV division type. Divide-by-zero and unknown operands use native SV behavior.

Evidence: tests/semantic_test.c checks common width/sign/state, original-width unsigned reinterpretation and mixed raw/numerical rejection. tests/cores/continuous_intrinsics* compare signed, unsigned, mixed-width and mixed-sign compound division/remainder with independent native references, including runtime zero divisors and X/Z operands. tests/native_array_test.py rejects numerical division/remainder updates and ordinary division, including sized raw divisors, while preserving output. One Icarus compound-divisor oracle needs the expanded assignment form (why/does/icarus/compound/division/disagree/with/systemverilog.md). The type owner is src/data_type.c; operation syntax alone never proves admissibility.
