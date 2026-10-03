---
status: green
revised_at: "2026-10-03T17:15:04+10:00"
checked_at: "2026-09-30T20:31:54+10:00"
---

The approved immediate work is the quarantined elastic RTL vertical slice.

1. Resolve the Task 5 rollback coverage gap in what/is/elastic/rtl/task/five.md: check an owner-constructable unbound PARAMETER with a fresh result type and genuine pre-call arena/memo snapshots, then recursive-child and allocation-failure paths. The existing green suite does not prove universal rollback.
2. Complete Task 6 declaration lowering in src/rtl_lower.c. BOUNDARY is implemented (section 8 of tests/rtl_lower_test.c is green); the net/variable realizations (COMBINATIONAL_NET / PROCEDURAL_VARIABLE — sections 9/10/11) and the storage realizations (ELASTIC_SLOT / PULSE_REGISTER / PARAMETERIZED_QUEUE / SKID_QUEUE — sections 12-15) still return the unimplemented sentinel, and the suite currently aborts at section 9's first red. Finish the remaining realization families in that order, then make the whole rtl-lower-test suite green.
3. Complete Task 7 whole-unit ready graph, Task 8 atomic transfers sharing one fire, Task 9 exact ordered output, Task 10 terminal SV emission, Task 11 quarantined composition/simulation, then Task 12 verification and accurate status. Exact contracts belong to the corresponding task leaves.
4. Complete retained-core, pipeline, FSM, child-instance and fabric migration and compatibility gates at what/are/the/remaining/architecture/migration/gates.md. Switch production only when every retained subsystem uses one structured path and its corresponding textual machinery is deleted in the same change. No validator bridge, fallback, second dialect or per-file choice.
5. Resolve remaining compiler verification failures, including the co-slice aggregate-width behavior in what/is/broken.md. Obtain the required Verilator toolchain through the user-authorized installation path, evaluate generated-SV lint findings as possible compiler defects, then establish a clean make verify.

Lowering consumes validated identities and backend-neutral realization properties. Semantic owners decide widths, types and conversions; RTL contains resolved hardware. Multi-arena builders must restore all counts on failure. Opaque SV occupies ordered spans copied at terminal emission. Ordinary branches and worktrees may isolate narrow checked work; neither linear history nor direct-to-master work is required.
