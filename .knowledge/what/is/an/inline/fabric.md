---
status: green
revised_at: "2026-11-11T16:24:16+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An inline fabric is a parent-module item connecting child output ports to child input ports. Named `fabric name begin ... endfabric` and anonymous `fabric begin ... endfabric` forms share lowering. Payload types and widths are checked per connection; no fabric-wide PAYLOAD_W exists. Payload/valid follow the producer-to-consumer arrow; ready returns upstream. Plain `>` is direct and exclusive; routed links support arbitrated source groups.

Fabric endpoints resolve through child connection metadata. Omitted ports receive generated parent signals; lowering recovers valid/ready from their boundary records. Explicit payload/control nets also work. Parent endpoint syntax and remaining coverage are owned by how/to/connect/child/ports.md.

Before RTL lowering, validate_fabric_ownership resolves each endpoint, checks output-to-input direction, and canonicalizes explicit output payload wires to typed endpoint views. A consuming fabric source cannot also feed an elastic named child input or a consuming parent transfer. A fabric destination cannot already have an upstream/child producer or a parent transfer writer. Non-consuming peek observation remains allowed. These checks cover mixed fabric/instance/local ownership; cross-fabric grouping, broader aliases and retained ordinary-SV drivers still need auditing.

make fabric-mixed-ownership-test covers child/raw-wire/local consumer conflicts, child/local producer conflicts, reversed direction and missing endpoints. child-port-endpoint-test covers a routed source observed through peek. Existing anonymous fairness, routed backpressure and two/three-source merge simulations remain passing.

Standalone fixed-width fabrics remain a separate production driver path, documented in how/to/write/prototype/fabrics.md.
