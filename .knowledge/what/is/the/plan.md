---
status: green
revised_at: "2026-10-04T19:52:53+11:00"
checked_at: "2026-09-30T20:31:54+10:00"
---

The approved immediate work is the quarantined elastic RTL vertical slice.

1. Task 8 atomic transfers sharing one fire (what/is/elastic/rtl/task/eight.md): its shape is established - the owner-level process/transfer enumeration accessors and the pigen_lower_rtl_transfers / pigen_lower_rtl_module entry points - and the remaining product steps are, in order: the fire-identity test contract (one shared conjunction per transfer, the join / repeated-projection / static-only-assignment / exclusive-producer / process-order gate families, make rtl-lower-test); the transfer-lowering implementation; the module-composition implementation; then the full-suite integration gate.
2. Task 9 exact ordered output, Task 10 terminal SV emission, Task 11 quarantined composition/simulation, then Task 12 verification and accurate status. Exact contracts belong to the corresponding task leaves. Task 7 (ready-cycle validation) is deferred to Pigen 1.0 (what/is/the/elastic/rtl/task/order.md).
3. Complete retained-core, pipeline, FSM, child-instance and fabric migration and compatibility gates at what/are/the/remaining/architecture/migration/gates.md. Switch production only when every retained subsystem uses one structured path and its corresponding textual machinery is deleted in the same change. No validator bridge, fallback, second dialect or per-file choice.
4. Fix the co-slice defect in what/is/broken.md (the last destination receives the whole aggregate; Verilator reports it as WIDTHTRUNC in coslice.sv and transfer_block.sv). Treat the remaining Verilator lint warnings from `make verify` as possible compiler defects, then make the lint fail on warnings so `make verify` cannot pass with them.
5. Retire the stub-era staged-red and sentinel-stub commentary still present in sections (7)-(15) of tests/rtl_lower_test.c: the file header and sections (3)-(6) already restate the landed Task 5/Task 6 lowering as current green facts, and the remaining sections are green against that same lowering, so those comments restate as current green facts without touching any assertion, line count or section structure (what/is/elastic/rtl/task/six.md owns the section contracts).

Lowering consumes validated identities and backend-neutral realization properties. Semantic owners decide widths, types and conversions; RTL contains resolved hardware. Compiler stages fail loudly: a stage reports its diagnostic and stops, with no rollback requirement for partially built state (how/should/compiler/builders/fail.md). Opaque SV occupies ordered spans copied at terminal emission. Ordinary branches and worktrees may isolate narrow checked work; neither linear history nor direct-to-master work is required.
