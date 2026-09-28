---
status: green
revised_at: "2026-09-28T18:53:06+10:00"
checked_at: "2026-09-14T17:13:16+10:00"
---

The approved immediate work is the quarantined elastic RTL vertical slice. Task 1's distinct IDs and arena and Task 2 canonical types and expressions are implemented; Task 2 review fixes are in 7593604. The Task 3 skeleton is landed: the resolved-hardware records and owner-range shape with the _with_owner constructor stubs are declared in include/pigen/rtl.h and src/rtl.c. Tasks 3 (test contract, then implementation) through 12 remain approved work: complete-module Task 3 test contract against the _with_owner names and Task 3 implementation with owner validation; collision-safe names; owner-based type/expression adapters; realization-owned declarations; whole-unit ready graph; atomic transfers sharing one fire; exact ordered output; terminal SV emission; quarantined composition/simulation; verification and accurate status. Exact contracts live in what/is/elastic/rtl/task/{one..twelve}.md. The next step is the Task 3 test contract (what/is/elastic/rtl/task/three.md), then its implementation, test-first with relevant gates, coherent commits and direct linear pushes to master, preserving production quarantine.

Lowering consumes validated identities and backend-neutral realization properties. RTL contains resolved hardware only; opaque SV occupies ordered spans copied at terminal emission. Multi-arena builders restore all counts on failure; semantic owners decide widths, types and conversions.

After the RTL slice, complete retained core, pipeline, FSM, child-instance and fabric migration and compatibility gates at what/are/the/remaining/architecture/migration/gates.md. Production waits until every retained subsystem uses one structured path and corresponding textual machinery is deleted in the same change. No validator bridge, fallback, second dialect or per-file choice. Work in narrow checked commits directly on master and push in linear history; David removed the PR workflow on 2026-09-14. Maintain current state and next in the tree.
