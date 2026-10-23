---
status: green
<<<<<<< HEAD
revised_at: "2026-10-23T21:13:09+11:00"
=======
revised_at: "2026-10-23T16:44:59+11:00"
>>>>>>> 92ef37cff181d35f477fe04acb1750a795127872
---

The structured frontend and elastic RTL work remain incomplete. Passing focused tests do not establish full compiler acceptance.

- **The elastic RTL vertical slice is complete.** Tasks 6 (declaration lowering), 8 (atomic fire/identity), 9 (ordered output), 10 (terminal SV emission), 11 (quarantined composition/simulation) and 12 (verification and accurate status) are all complete; the terminal emitter renders a finished, named RTL lowering to SystemVerilog validate-first, committing no partial output, and `make verify` exits 0 on master. No further in-order step remains; Task 7 (ready-cycle validation) is deferred to Pigen 1.0. The declaration-lowering contract and evidence are owned by `what/is/elastic/rtl/task/six.md`; the complete vertical-slice state is owned by `what/is/the/plan.md`.
- **Structured frontend is unlinked from production ./pigen.** The production Makefile target still compiles the textual prototype rather than the structured syntax, resolution, semantic and RTL path. See does/the/structured/frontend/run/in/production.md.
- **Retained-subsystem build and frontend gaps.** Core, pipeline, FSM, child-instance and fabric are specified from the spec and built on the structured path, subject to what/are/the/remaining/architecture/migration/gates.md. Accepted parameter/type/aggregate/array/expression forms and macro concatenation/stringification/required arguments remain incomplete. Delete textual machinery only when the corresponding retained subsystem's structured path covers the spec.
<<<<<<< HEAD
- **Pipeline step-(c) seam under-specified; the step-(c2) manifest is not a satisfiable (c3) contract.** The (c1) seam `pigen_pipeline_lower(lowering, module, syntax)` carries only the lowering context (the semantic and RTL models) plus the module and the pipeline node id; it carries no syntax tree, no expanded source, and no per-pipeline record, and the pipeline's fields are syntax-tree nodes, not semantic signals. The (c2) correction reconciled the *red* witnesses (the four source-level stage-law reds became seam-reachable node-kind reds: a non-`PIGEN_SYNTAX_PIPELINE` id is rejected, `rc == -1`), but the *anchor* witnesses still cannot be met by any model-only `src/pipeline_lower.c`: the emitted-member-order anchor pins an RTL concatenation that must render the ordered field spelling `{x,y}`, yet the only concatenation a model-only lowering can build is the step-(a) width-keyed outgoing-packet aggregate, which carries no per-field children and renders as the plain signal name, and the one-per-stage-boundary buf-instance count and the per-stage next-value-equation count require the stage count, which the semantic model does not expose. The product decision is to extend the seam to carry the syntax tree/pipeline node (consistent with the step-(a)/(b) siblings, both of which take `const pigen_syntax_node *pipeline`) rather than re-scope the product contract; the step-(c3) implementation waits on the extended seam (what/is/the/pipeline/parent/module/lowering.md).
=======
- **Pipeline step (c) implementation is pending.** The step-(c) lowering owner seam (c1, include/pigen/pipeline_lower.h + src/pipeline_lower.c) and its corrected discriminating manifest (c2, tests/pipeline_lower_test.c, target pipeline-lower-test) are established; the implementation to that manifest (c3) and its landing (c4) remain. The manifest's green anchors are hidden-red against the (c1) fail-loud stub and go green when (c3) lands; its red forms are seam-reachable node-kind rejections (a non-pipeline syntax id) the seam itself must reject. The step-(b) source-level stage laws (first-stage uninitialized-field read, unequal-total-$bits boundary) are covered where they can be driven: pipeline-stage-test (target) is green and discriminating.
>>>>>>> 92ef37cff181d35f477fe04acb1750a795127872

With Verilator 5.020 and Icarus 13.0 installed, `make verify` exits 0 on master.
