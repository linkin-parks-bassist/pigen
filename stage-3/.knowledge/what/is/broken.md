---
status: green
revised_at: "2026-10-23T16:44:59+11:00"
---

The structured frontend and elastic RTL work remain incomplete. Passing focused tests do not establish full compiler acceptance.

- **The elastic RTL vertical slice is complete.** Tasks 6 (declaration lowering), 8 (atomic fire/identity), 9 (ordered output), 10 (terminal SV emission), 11 (quarantined composition/simulation) and 12 (verification and accurate status) are all complete; the terminal emitter renders a finished, named RTL lowering to SystemVerilog validate-first, committing no partial output, and `make verify` exits 0 on master. No further in-order step remains; Task 7 (ready-cycle validation) is deferred to Pigen 1.0. The declaration-lowering contract and evidence are owned by `what/is/elastic/rtl/task/six.md`; the complete vertical-slice state is owned by `what/is/the/plan.md`.
- **Structured frontend is unlinked from production ./pigen.** The production Makefile target still compiles the textual prototype rather than the structured syntax, resolution, semantic and RTL path. See does/the/structured/frontend/run/in/production.md.
- **Retained-subsystem build and frontend gaps.** Core, pipeline, FSM, child-instance and fabric are specified from the spec and built on the structured path, subject to what/are/the/remaining/architecture/migration/gates.md. Accepted parameter/type/aggregate/array/expression forms and macro concatenation/stringification/required arguments remain incomplete. Delete textual machinery only when the corresponding retained subsystem's structured path covers the spec.
- **Pipeline step (c) implementation is pending.** The step-(c) lowering owner seam (c1, include/pigen/pipeline_lower.h + src/pipeline_lower.c) and its corrected discriminating manifest (c2, tests/pipeline_lower_test.c, target pipeline-lower-test) are established; the implementation to that manifest (c3) and its landing (c4) remain. The manifest's green anchors are hidden-red against the (c1) fail-loud stub and go green when (c3) lands; its red forms are seam-reachable node-kind rejections (a non-pipeline syntax id) the seam itself must reject. The step-(b) source-level stage laws (first-stage uninitialized-field read, unequal-total-$bits boundary) are covered where they can be driven: pipeline-stage-test (target) is green and discriminating.

With Verilator 5.020 and Icarus 13.0 installed, `make verify` exits 0 on master.
