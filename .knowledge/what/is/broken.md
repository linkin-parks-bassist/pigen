---
status: green
revised_at: "2026-10-24T10:38:50+11:00"
---

The structured frontend and elastic RTL migration remain incomplete. Passing focused tests does not establish full compiler acceptance.

- The production Makefile target builds the textual prototype, excluding the structured syntax/resolution/semantic/RTL path. See `does/the/structured/frontend/run/in/production.md`.
- The inspected working checkout's composer treats semantic resolution's success return (1) as failure. Its compile tests still expect failure and its vertical-slice simulation target invokes the prototype CLI. Task 11 composition/simulation and Task 12 accurate integration status are therefore not established by those tests. The newer master commit has not been compared for this defect; do not generalize checkout evidence into verified master status.
- Retained pipeline, FSM, child-instance and fabric migration and accepted parameter/type/aggregate/array/expression/macro forms remain subject to `what/are/the/remaining/architecture/migration/gates.md`. Focused owner seams are not evidence of their integration into the composed compiler.

Terminal SV emission and other backend components exist separately. A green prototype verification run does not establish a successful structured compilation or complete retained-feature coverage. The remaining work and open decisions are owned by `what/is/the/plan.md`.
