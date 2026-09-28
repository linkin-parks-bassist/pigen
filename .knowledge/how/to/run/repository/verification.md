---
status: green
revised_at: "2026-09-27T15:07:06+10:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

make verify includes structured C tests, prototype smoke/fabric/core/pipeline/scope/syntax/biquad/co-slice/slicing/signal/validity/signed tests plus RTL ready breaks, waveform/output/BRAM/clear/FSM simulations. It needs Icarus (iverilog/vvp) and Verilator. Several targets overwrite tracked examples SV/VCD; preserve unrelated edits and investigate churn. make clean removes only pigen.

Current results and blockers are recorded in where/am/i.md and why/does/pipeline/verification/currently/fail.md.
