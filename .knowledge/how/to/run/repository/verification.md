---
status: green
revised_at: "2026-10-03T23:53:33+10:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

make verify includes structured C tests, prototype smoke/fabric/core/pipeline/scope/syntax/biquad/co-slice/slicing/signal/validity/signed tests plus RTL ready breaks, waveform/output/BRAM/clear/FSM simulations. It needs Icarus Verilog 13 (`~/.local/bin`; the Ubuntu 12.0 package mis-elaborates `$bits(<signal>)`) and Verilator (5.020, installed). It passes with exit 0 on this host. Its Verilator lint uses `-Wno-fatal`, so read the `%Warning` lines in its output; they are not failures yet (what/is/broken.md). Several targets overwrite tracked examples SV/VCD; preserve unrelated edits and investigate churn. make clean removes only pigen.

Proof:

```bash
command -v verilator >/dev/null && command -v iverilog >/dev/null
```
