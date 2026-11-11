---
status: green
revised_at: "2026-11-11T15:51:27+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An inline fabric is a parent-module item connecting child output-port identities to child input-port identities in that parent. Named `fabric name begin ... endfabric` and anonymous `fabric begin ... endfabric` forms use the same lowering. Payload types and widths are checked per connection; no fabric-wide PAYLOAD_W exists. Routed arrows point from producer to consumer; payload/valid follow the arrow and ready returns upstream. Plain `>` is direct and exclusive.

Fabric endpoints resolve through child connection metadata. Omitted ports receive generated parent signals; lowering recovers valid/ready from their boundary records. Explicit parent payload/control nets also work. Anonymous delivery/fairness tests and implicit-endpoint routed backpressure simulations pass. Parent endpoint syntax and remaining coverage are owned by how/to/connect/child/ports.md.

Standalone fixed-width fabrics remain a separate production driver path, documented in how/to/write/prototype/fabrics.md.
