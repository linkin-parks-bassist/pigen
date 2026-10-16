---
status: green
revised_at: "2026-10-16T19:05:19+11:00"
---

The structured frontend and elastic RTL work remain incomplete. Passing focused tests do not establish full compiler acceptance.

- **The elastic RTL vertical slice is complete.** Tasks 6 (declaration lowering), 8 (atomic fire/identity), 9 (ordered output), 10 (terminal SV emission), 11 (quarantined composition/simulation) and 12 (verification and accurate status) are all complete; the terminal emitter renders a finished, named RTL lowering to SystemVerilog validate-first, committing no partial output, and `make verify` exits 0 on master. No further in-order step remains; Task 7 (ready-cycle validation) is deferred to Pigen 1.0. The declaration-lowering contract and evidence are owned by `what/is/elastic/rtl/task/six.md`; the complete vertical-slice state is owned by `what/is/the/plan.md`.
- **Structured frontend is unlinked from production ./pigen.** The production Makefile target still compiles the textual prototype rather than the structured syntax, resolution, semantic and RTL path. See does/the/structured/frontend/run/in/production.md.
- **Retained-subsystem build and frontend gaps.** Core, pipeline, FSM, child-instance and fabric are specified from the spec and built on the structured path, subject to what/are/the/remaining/architecture/migration/gates.md. Accepted parameter/type/aggregate/array/expression forms and macro concatenation/stringification/required arguments remain incomplete. Delete textual machinery only when the corresponding retained subsystem's structured path covers the spec.

With Verilator 5.020 and Icarus 13.0 installed, `make verify` exits 0 on master.
