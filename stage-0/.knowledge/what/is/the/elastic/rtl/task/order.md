---
status: green
revised_at: "2026-10-04T00:24:32+10:00"
checked_at: "2026-09-29T09:44:09+10:00"
---

The approved order of the elastic RTL tasks is: distinct RTL identities/arena; canonical RTL types/expressions; modules/hardware/equations/processes; names; type/expression owner adapters; realization lowering; transfers from one fire identity; ordered output; terminal emitter; quarantined composer/simulation; verification/status boundary.

The ready-graph task (Task 7, combinational ready-cycle validation) is deferred to Pigen 1.0, after the structured compiler works end to end: it validates designs but nothing else depends on it, and Verilator's lint flags combinational loops in emitted SV meanwhile. When it is taken up, plain cycle detection that names one transfer in the loop suffices. The original Tarjan and deterministic-diagnostic requirements in what/is/elastic/rtl/task/seven.md are not required.

This leaf owns the approved order only; current task status and the next task are owned by what/is/the/plan.md and where/am/i.md. Each task has discriminating focused tests and a coherent green commit; per-task contracts and implementation evidence live in the narrow task leaves.

Related: [what is elastic rtl task one](../../../../elastic/rtl/task/one.md), [what is the plan](../../../plan.md).
