---
status: green
revised_at: "2026-11-09T15:30:11+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Run make, then ./pigen design.pigen -o design.sv. The production CLI uses pigen_compile_source and the structured frontend; no prototype fallback or per-file dialect choice is approved. Compile emitted SV alongside rtl/pigen_primitives.sv when storage primitives are instantiated. Verification uses Icarus Verilog and Verilator; installed tool qualifications are in where/am/i.md.

Use data-first declarations (how/to/write/runnable/prototype/declarations.md), keep Pigen actions in one supported edge domain, and inspect emitted RTL and simulate intended behavior before relying on CLI success. Focused structured tests pass, but prototype-dialect corpus targets remain migration debt and accepted CLI inputs have known semantic gaps (what/is/broken.md). The examples and old corpus are not a runnable production-language recipe. Structured pipeline/FSM status and remaining child-instance/fabric work are owned by what/is/the/plan.md.

For an isolated experiment, build a pinned source snapshot outside active worktrees, copy its compiler and required RTL into the experiment project, and retain source revision and binary hashes. Do not silently substitute prototype behavior when the structured route rejects or omits a construct.
