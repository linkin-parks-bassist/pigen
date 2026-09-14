---
status: "unverified"
created_at: "2026-09-13T15:05:47+10:00"
scope: "local"
source: "David's current-only knowledge instruction; current Makefile and owner contracts, Codex /root 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:49:23+10:00"
---

Modules and resolved hardware. Add module objects, instances, equations, processes and enabled updates with owner ranges. Instances own ordered parameters/connections; processes own updates. Semantic IDs are provenance and can be invalid for synthetic objects. Gate: complete module order/provenance; reject foreign destination equations, connections and updates without changing arena counts or owner ranges; make rtl-test. This is approved future work, not implemented status.

Exact planned constructors are pigen_rtl_module_add(model, semantic_module, span); pigen_rtl_object_add(model, module, kind, type, direction, semantic_signal, span); pigen_rtl_process_add(model, module, clock_expression, edge, updates, update_count, span). Task 3 extends the current origin-only constructors with resolved owner identities. Build input/internal objects, equation, primitive instance and process/update in one complete-module test. Module owns object/instance/equation/process ranges, instance owns ordered parameter/connection ranges, process owns update ranges. Validate owner identity before publishing. Cross-module failures leave both arena counts and owner ranges unchanged. Named files are rtl.h, rtl.c, rtl_test.c.
