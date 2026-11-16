---
status: green
revised_at: "2026-11-15T14:22:00+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

RTL modules own objects, instances, equations and processes through half-open arena ranges. Instances own ordered parameter/connection ranges; clocked processes own ordered enabled updates. Combinational processes own typed statement bodies (how/are/scoped/combinational/processes/represented.md). Semantic IDs are provenance and may be invalid for synthetic hardware. include/pigen/rtl.h and src/rtl.c define the records and constructors.

Owner-carrying constructors validate owners and referenced records before publication. Foreign destination equations, instance connections and updates fail without changing arena counts or owner ranges. Instance range_append checks overflow and range/count divergence; copied parameters/connections remain model-owned.

Equations retain destination, value, module, origin and optional destination_expression. Ordinary equations leave the expression invalid. For compound targets, destination is only the first leaf object for compatibility bookkeeping; it is not the complete writer set. pigen_rtl_equation_add_projection_with_owner accepts an object, an index/select chain rooted in an object, or nested concatenations of those lvalues. Every target leaf must belong to the requested module. Literals, non-lvalue leaves and foreign leaves reject without publishing an equation. The complete target expression retains native concatenation ordering and is rendered as an lvalue; RHS assignment conversion occurs before emission.

pigen_rtl_equation_drives_object(model,equation,object) checks every target leaf. Index/select coordinates are read operands and do not count as destinations. Whole-object equations use destination directly. src/rtl_lower.c endpoint defaults use this query so a child input in any concatenation position retains its explicit driver instead of receiving a competing zero default. The emitter validates/renders destination_expression when present, preserving neighboring selected bits.

Evidence: make rtl-test checks owner/arena contracts, selected and nested concatenated targets, first-leaf bookkeeping, complete driver queries, index-read exclusion, and non-lvalue/foreign-leaf rejection without publication. child-port-endpoint-test simulates selected ready targets with neighboring-bit preservation. tests/native_array_test.py generate_assign_child independently checks a child input as the second concatenation leaf across all AXI families under stalls/reset. tests/cores/continuous_intrinsics* score nested destinations, signed widening, truncation and observation without token consumption. Origin-only constructors remain available alongside owner-carrying forms.
