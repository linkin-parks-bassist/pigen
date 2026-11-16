---
status: green
revised_at: "2026-11-15T14:53:18+11:00"
checked_at: "2026-10-01T18:31:08+10:00"
---

Pipeline verification passes with the structured production compiler and the installed Icarus Verilog 13 and Verilator 5.020. The complete make verify gate passes, including the pipeline and corpus gates. Current acceptance boundaries belong to what/is/broken.md and the verification procedure to how/to/run/repository/verification.md.

Icarus 12 is unsuitable for the historical signal-width reproducer: `module m(input logic [7:0] s); logic [$bits(s)-1:0] p; assign p=s; endmodule`. Its elaborator failed on a signal-derived declaration range, while alternative spellings could simulate unknown/high-impedance values. The installed Icarus 13 handles that case. Keep the newer simulator on PATH; changing the spelling of legal SV to evade an older simulator defect is not a compiler semantics fix.

Evidence: make verify builds and runs the pipeline corpus, examples, output models and ordinary fabric regressions. Its covered targets and installed tool versions are specified in how/to/run/repository/verification.md. An old failure on a different checkout or simulator version is not evidence that the current pipeline gate fails.
