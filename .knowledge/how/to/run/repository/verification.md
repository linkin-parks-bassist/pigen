---
status: green
revised_at: "2026-10-26T09:40:04+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

make verify includes structured C tests, prototype smoke/fabric/core/pipeline/scope/syntax/biquad/co-slice/slicing/signal/validity/signed tests plus RTL ready breaks, waveform/output/BRAM/clear/FSM simulations. It needs Icarus Verilog 13 (`~/.local/bin`; the Ubuntu 12.0 package mis-elaborates `$bits(<signal>)`) and Verilator (5.020, installed). It is currently red on this host: it stops at the first corpus target (fabric-test, "pigen: RTL naming failed", exit 2) because the corpus .pigen files use the prototype dialect the structured frontend diagnoses by design; those targets go red as per-subsystem migration debt while the structured C test targets stay green (what/is/broken.md). Its Verilator lint runs without -Wno-fatal, so a lint warning fails the target (pinned by tests/lint_fatal_pin.sh). Several targets overwrite tracked examples SV/VCD; preserve unrelated edits and investigate churn. make clean removes only pigen.

Proof:

```bash
command -v verilator >/dev/null && command -v iverilog >/dev/null
```
