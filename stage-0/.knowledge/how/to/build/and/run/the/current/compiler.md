---
status: green
revised_at: "2026-09-14T20:49:23+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Run make, then ./pigen design.pigen -o design.sv. Compile emitted SV alongside rtl/pigen_primitives.sv. Full verification needs Icarus Verilog (iverilog/vvp) and Verilator. These commands use the quarantined production prototype, not the unlinked structured frontend. Target data-first forms are specified in semantic language owners, and runnable prototype declarations/fabrics have their own direct how-to leaves. Start with named buf stages, add FIFO for burst/depth and skid for exactly-two-slot timing absorption, compile early/read emitted RTL and keep Pigen actions in one supported edge domain. Prefer direct <= routes for flow and valid/ready/accepts for control observations. make verify and examples/waveform/BRAM/FSM targets provide evidence, not a complete future-language claim.
