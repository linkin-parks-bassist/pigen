---
status: green
revised_at: "2026-11-11T16:35:41+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

RTL modules own objects, instances, equations and processes through half-open arena ranges. Instances own ordered parameter/connection ranges; processes own ordered enabled updates. Semantic IDs are provenance and may be invalid for synthetic hardware. include/pigen/rtl.h and src/rtl.c define the records and constructors.

Owner-carrying constructors validate their owner and referenced records before publishing. Foreign destination equations, instance connections and updates fail without changing arena counts or owner ranges. Instance range_append checks size overflow and range/count divergence; copied parameters/connections remain model-owned.

Equations retain a base destination object and an optional destination_expression. Ordinary equations leave that expression invalid. pigen_rtl_equation_add_projection_with_owner accepts an object or index/select chain rooted in an object belonging to the requested module; literals and other non-lvalues fail without publication. The emitter validates and renders the selected target instead of assigning the whole base object. This supports child output-ready bindings to selected parent bits without overwriting neighbors.

make rtl-test covers arena/owner contracts and projected-target rejection/publication. child-port-endpoint-test simulates selected ready targets through sibling and fabric connections, checking ordering under stalls and preservation of a separately driven neighboring bit. Origin-only constructors remain available alongside owner-carrying forms.
