---
status: green
revised_at: "2026-11-15T17:25:24+11:00"
---

A native Stream fabric can widen TID while preserving the source ID in its low bits. High bits hold the source's ordinal within the destination's incoming edge group, following source-elaboration order across root items and selected generate occurrences. A target needs source ID width plus ceil(log2(source count)) bits; extra high bits zero-extend. Omitted source IDs contribute zero low bits. Other fields retain identity/value. Child connections and ordinary channel copies require exact contracts.

src/protocol.c:pigen_protocol_can_route permits sufficient ID widening with otherwise identical Stream fields, and also permits exact contracts. Exact-ID fan-in with LAST retains one origin until all of its open (TID,TDEST) packets close, rather than rotating at the first TLAST. Its namespace bitmap is bounded by the generated-bit policy; oversized storage diagnoses and recommends target-ID widening. Mechanism, progress conditions and witnesses: how/are/exact-width/stream/fan-in/packets/tracked.md. Without LAST, arbitration releases after each accepted beat.

At successive widening merges, each stage preserves the entire incoming ID as its low field and adds its own ordinal above it. Hierarchical tags follow per-stage authored order and may have unused codes; they need not equal flat input indices. Single-source widening zero-extends TID. AXI4 memory-mapped ID prefix/response stripping remains separate (how/are/native/axi/fabrics/connected/and/arbitrated.md).

Switched merges require provenance space even when an exact contract would otherwise match. src/resolve.c:validate_stream_switch_contracts checks the completed graph: a destination with several inputs needs tags when any input has several outgoing destinations. Late fan-out is covered independent of source order. Insufficient TID diagnoses before output publication.

src/fabric_lower.c repacks each beat into the target layout before arbitration, injecting its group index and retaining canonical field offsets/types. LAST offsets use the target layout after widening. Switched merges configure PACKET_LOCK=0 and rotate after accepted beats, avoiding packet-grant cycles while preserving stalled grants. Plain fan-in uses PACKET_LOCK=1; exact-ID packet tracking additionally enables TRACK_STREAMS. TDEST routes: how/does/the/native/axi/stream/destination/switch/work.md.

The stream-identity/interleaving basis is Arm IHI 0051B §§2.7/4.1 and AMBA 4 IHI 0051A ([B](https://documentation-service.arm.com/static/64819f1516f0f201aa6b963c?token=), [A](https://documentation-service.arm.com/static/642583d7314e245d086bc8c9)).

Evidence: tests/native_axis_routing_test.py checks colliding/absent IDs, TID/TDEST interleaving, reopening keys, the full 128-key namespace, source-order tags, sidebands, stalls and reset after partial packets. Exact-ID child, standalone, generated and collection compositions pass. Five/seven-source two-stage trees exercise reversed group order and cumulative tag space; insufficient outer tags reject. tests/protocol_test.c checks widening and exact child compatibility. examples/native_axi_relay.pigen:native_axis_switch2x3 reserves a fifth target ID bit for two four-bit-ID inputs.
