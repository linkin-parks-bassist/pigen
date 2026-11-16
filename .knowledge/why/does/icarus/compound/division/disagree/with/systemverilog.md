---
status: green
revised_at: "2026-11-15T15:05:36+11:00"
---

The installed Icarus 13 truncates a wider compound-assignment divisor prematurely in the checked reproducer. With `reg [7:0] a,b; a=1; b=1; a/=16'h0101; b=b/16'h0101;`, Icarus reports a=1 and b=0. Verilator 5.020 reports both zero. IEEE 1800-2017 §11.4.1 defines compound assignment as equivalent to the expanded blocking assignment except that the left-hand index is evaluated once, so zero is the required result ([IEEE text](https://fpga.mit.edu/6205/_static/F25/documentation/1800-2017.pdf)).

Keep the compiler's full divisor width. tests/cores/continuous_intrinsics_tb.sv uses the expanded native expression `8'(source/16'h0101)` as this case's independent oracle, with a comment explaining the simulator distinction. Other compound operations retain direct native-SV reference statements. tests/cores/run.py lints the generated compound_comb design with warnings-fatal Verilator and runs the complete reference comparison with Icarus. This workaround belongs to the test oracle; it does not narrow the production arithmetic.

Evidence: the minimal native reproducer was compiled and run separately with both installed simulators. The persistent compound regression covers all byte inputs, wider divisors, mixed signedness, divide-by-zero and X/Z operands. A different simulator version needs fresh qualification before this exception is removed.
