---
status: "unverified"
created_at: "2026-09-13T15:03:19+10:00"
scope: "local"
source: "Makefile"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:26+10:00"
---

make verify includes structured C tests, prototype smoke/fabric/core/pipeline/scope/syntax/biquad/co-slice/slicing/signal/validity/signed tests plus RTL ready breaks, waveform/output/BRAM/clear/FSM simulations. It needs Icarus (iverilog/vvp) and Verilator. Several targets overwrite tracked examples SV/VCD; preserve unrelated edits and investigate churn. make clean removes only pigen.

Related: [what was checked during knowledge ingestion](../../../../what/was/checked/during/knowledge/ingestion.md).
