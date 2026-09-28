---
status: green
revised_at: "2026-09-28T23:26:54+10:00"
checked_at: "2026-09-14T17:13:16+10:00"
---

The approved immediate work is the quarantined elastic RTL vertical slice. Task 1's distinct IDs and arena and Task 2 canonical types and expressions are implemented; Task 2 review fixes are in 7593604. Task 3 is implemented and landed: the six _with_owner resolved-hardware constructors in src/rtl.c validate owner identity before publication, publish half-open owner ranges at all three levels, and reject foreign-destination equations/connections/updates without changing arena counts or owner ranges. The corrected Task 3 test contract passes; make rtl-test is green (both PASS lines) with every Task 1/2 case unaffected.

Remaining approved work, in order: Task 4 collision-safe names; Task 5 owner-based type/expression adapters; Task 6 realization-owned declarations; Task 7 whole-unit ready graph; Task 8 atomic transfers sharing one fire; Task 9 exact ordered output; Task 10 terminal SV emission; Task 11 quarantined composition/simulation; Task 12 verification and accurate status. Exact contracts live in what/is/elastic/rtl/task/{one..twelve.md}.

Lowering consumes validated identities and backend-neutral realization properties. RTL contains resolved hardware only; opaque SV occupies ordered spans copied at terminal emission. Multi-arena builders restore all counts on failure; semantic owners decide widths, types and conversions.

After the RTL slice, complete retained core, pipeline, FSM, child-instance and fabric migration and compatibility gates at what/are/the/remaining/architecture/migration/gates.md. Production waits until every retained subsystem uses one structured path and corresponding textual machinery is deleted in the same change. No validator bridge, fallback, second dialect or per-file choice. Work in narrow checked commits directly on master and push in linear history; David removed the PR workflow on 2026-09-14. Maintain current state and next in the tree.
