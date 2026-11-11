---
status: green
revised_at: "2026-11-11T15:43:53+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An inline fabric is a parent-module item connecting child output-port identities to child input-port identities in that parent. Named `fabric name begin ... endfabric` and anonymous `fabric begin ... endfabric` forms use the same lowering. Payload types and widths are checked per connection; no fabric-wide PAYLOAD_W exists. Routed arrows point from producer to consumer; payload/valid follow the arrow and ready returns upstream. Plain `>` is direct and exclusive.

Parent-wide `instance.port` endpoint access and automatically generated parent nets are required by what/is/the/spec.md. The present fabric lowerer still obtains endpoints from child connection metadata; explicitly connected payload and valid/ready nets are supported. Explicit control expressions retain semantic values for fabric lowering, while implicit generated boundary controls remain emission-owned. Anonymous routed delivery and fairness are exercised by fabric-anonymous-test.

Standalone fixed-width fabrics remain a separate production driver path, documented in how/to/write/prototype/fabrics.md.
