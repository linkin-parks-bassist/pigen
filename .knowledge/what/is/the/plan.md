---
status: green
revised_at: "2026-10-04T09:25:44+11:00"
checked_at: "2026-09-30T20:31:54+10:00"
---

The approved immediate work is the quarantined elastic RTL vertical slice.

1. Finish removing failure rollback from the elastic RTL lowering, per the approved fail-loudly decision (how/should/compiler/builders/fail.md): the post-failure state-comparison assertions are already gone from tests/rtl_lower_test.c (failure-report and idempotence assertions kept); what remains is removing the snapshot/restore code in src/rtl_lower.c that exists only for them.
2. Close the storage test-contract coverage gap: the storage matrix (section 12) pins payload kind/type and the INTERNAL direction only, and the input side is never pinned to the payload object, so a direction-ignoring or input-side-dropping storage lowering passes sections 12-15. Extend section 12 to build a non-INTERNAL (PIGEN_SEMANTIC_OUTPUT) concrete storage signal and pin payload->direction to it, and pin the input side (input_payload.index == payload.index, input_valid/input_ready the same object identity); tests only, sections 13-15 unchanged.
3. Complete Task 8 atomic transfers sharing one fire, Task 9 exact ordered output, Task 10 terminal SV emission, Task 11 quarantined composition/simulation, then Task 12 verification and accurate status. Exact contracts belong to the corresponding task leaves. Task 7 (ready-cycle validation) is deferred to Pigen 1.0 (what/is/the/elastic/rtl/task/order.md).
4. Complete retained-core, pipeline, FSM, child-instance and fabric migration and compatibility gates at what/are/the/remaining/architecture/migration/gates.md. Switch production only when every retained subsystem uses one structured path and its corresponding textual machinery is deleted in the same change. No validator bridge, fallback, second dialect or per-file choice.
5. Fix the co-slice defect in what/is/broken.md (the last destination receives the whole aggregate; Verilator reports it as WIDTHTRUNC in coslice.sv and transfer_block.sv). Treat the remaining Verilator lint warnings from `make verify` as possible compiler defects, then make the lint fail on warnings so `make verify` cannot pass with them.

Lowering consumes validated identities and backend-neutral realization properties. Semantic owners decide widths, types and conversions; RTL contains resolved hardware. Compiler stages fail loudly: a stage reports its diagnostic and stops, with no rollback requirement for partially built state (how/should/compiler/builders/fail.md). Opaque SV occupies ordered spans copied at terminal emission. Ordinary branches and worktrees may isolate narrow checked work; neither linear history nor direct-to-master work is required.
