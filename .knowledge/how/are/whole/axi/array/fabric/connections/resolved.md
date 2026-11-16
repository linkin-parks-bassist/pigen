---
status: green
revised_at: "2026-11-15T10:20:38+11:00"
---

src/resolve.c add_fabric_protocol_arrays resolves both fabric expressions through native collection views in the active lexical scope (how/are/native/axi/collection/selections/resolved.md). It requires equal rank, element count and evaluated left/right bounds in every dimension. Coordinates retain their declared direction. A scalar/collection pair rejects; the link does not broadcast or reshape collections. When neither operand is a collection, normal scalar/ordinary fabric resolution continues.

The view constructor selects canonical lane IDs and translates child lanes through existing instance views. The fabric helper publishes an edge for each corresponding selected pair. Every lane uses add_fabric_protocol_edge: complete route compatibility, roles, clock/reset identity, consumer ownership and ID-tag capacity remain checked. Normal and selected generated fabric bodies share this path.

One source statement publishes multiple pigen_semantic_protocol_fabric_edge records with the original syntax/provenance. Existing lowering consumes those scalar records; there is no separate array primitive or source-text expansion. Direct links remain exclusive. Multiple source collections may route to a destination collection, forming a separate arbitration group per lane. Lanes retain independent channels and storage; this is not an atomic array-token transfer.

Evidence: tests/native_array_test.py independently transports Stream/Lite/full AXI through direct/routed collection links, sliced rows, child arrays, named generated child arrays and typed standalone fabrics under stalls/reset. Negatives check bounds/direction/rank/cardinality mismatches, scalar mixing, duplicate consumers and competing transfers while preserving output. tests/generate_semantic_test.c checks canonical per-lane edge identities and two-source fan-in in module/generated scopes; its ASan/UBSan run passes. Collection fan-in cases in native_axis_fabric_test.py and native_axi_fabric_test.py check packet ownership, full AXI source tags, complete transactions and queued reset. examples/native_axi_generated.pigen includes native_axis_array_link. Array representation is owned by how/are/native/axi/endpoint/arrays/represented.md; routing laws by how/are/native/axi/fabrics/connected/and/arbitrated.md.
