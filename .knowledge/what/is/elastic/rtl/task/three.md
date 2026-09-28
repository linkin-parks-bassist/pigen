---
status: green
revised_at: "2026-09-28T23:26:00+10:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Modules and resolved hardware. Module objects, instances, equations, processes and enabled updates with owner ranges. Instances own ordered parameters/connections; processes own updates. Semantic IDs are provenance and can be invalid for synthetic objects. Gate: complete module order/provenance; reject foreign destination equations, connections and updates without changing arena counts or owner ranges; make rtl-test.

Task 3 is implemented and landed on master. The six _with_owner constructors in src/rtl.c validate owner identity and every referenced child before any arena is touched, so a failing add publishes nothing and leaves all arena counts and existing owner ranges unchanged. They publish half-open owner ranges: module over objects/instances/equations/processes, instance over ordered parameters/connections (copied into model-owned flat arenas with rollback on overflow or foreign-connection failure), and process over the trailing update range. The instance parameter/connection range_append helper guards size overflow and range/count divergence. The corrected test contract (synthetic-provenance orphan on the synthetic span, object-count assertions counting the block's five objects, final sweep counting three modules) passes; make rtl-test is green with both PASS lines, with Task 1/2 cases unaffected.

The origin-only module/object/instance/equation/update/process constructors remain declared alongside the owner-carrying forms; they may be folded into or retired in favor of the _with_owner forms in a later task.

Task 4 (collision-safe names) is next.
