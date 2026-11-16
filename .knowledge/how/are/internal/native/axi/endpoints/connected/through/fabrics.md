---
status: green
revised_at: "2026-11-17T10:33:00+11:00"
---

AXI fabrics connect declared module/fabric ports and child-port views. Standalone internal native endpoints are rejected, so s -> middle -> m with a locally declared AXI middle is not valid syntax.

Connect s directly to m or to child.bus. An explicit buffering module can declare AXI input/output ports and channel storage; its parent connects those boundaries through ordinary child bindings or fabric links. Inside datapaths, unpack channel records into ordinary transfer types. how/are/internal/native/axi/child/bindings/resolved.md owns child access and the declaration restriction.

src/resolve.c:add_fabric_protocol_edge requires fixed source/target roles, complete routing compatibility, matching clock/reset bindings and exclusive transfer ownership. There is no owned-internal role exception. Direct and routed links retain protocol-specific channel directions, address decoding, ID provenance and arbitration. how/are/native/axi/fabrics/connected/and/arbitrated.md owns those laws.

The native fabric/core test drivers retain boundary, child, typed-standalone and multi-hop profiles. Internal-storage transit profiles have been removed because their declarations violate the boundary policy.
