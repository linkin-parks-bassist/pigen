---
status: green
revised_at: "2026-11-17T12:38:08+11:00"
---

Native AXI fabric integration is quarantined and not in use. The design below describes retained inactive code and pre-quarantine witnesses; it is not current compiler support. Module boundaries remain active. Quarantine ownership: how/are/native/axi/fabrics/connected/and/arbitrated.md.

## Retained design and witnesses

A native Stream merge with unchanged TID width and TLAST retains one input grant until that input's entire open packet set closes. A stream key is the complete (TID,TDEST) pair. Each accepted non-last beat opens its key if necessary; a matching TLAST closes it. A single-beat packet does not close another key. The first TLAST alone cannot release a grant while other keys remain open, preventing different origins from merging into one packet.

rtl/pigen_primitives.sv:pigen_axis_merge maintains one open-key bitmap and count for the selected origin. It rotates after the final open key closes, or for a single-beat packet when none are open. Grant and payload remain stable during output stalls. Reset clears bitmap/count, ownership and receive queues. An input that perpetually keeps some packet open can retain the grant indefinitely; round-robin progress requires packet-set closure. This conservative rule avoids cross-input ownership cycles when FIFO heads belong to streams owned by other inputs.

The bitmap has 2^(TID width + TDEST width) entries; absent fields contribute zero key bits, giving one entry when both are absent. src/resolve.c:validate_stream_switch_contracts checks this storage width against maximum_generated_bits on the completed graph. Oversized tracking diagnoses before output publication and recommends target-ID widening for provenance. Single-source links, absent TLAST and widened-ID merges need no bitmap. General identity/tag rules: how/are/native/stream/fabric/identities/preserved.md.

src/fabric_lower.c enables TRACK_STREAMS for exact-ID, packet-locked merges with LAST and supplies canonical ID/DEST offsets and widths. AXIS_MERGE RTL parameters are N, LAST_BIT, HAS_LAST, PACKET_LOCK, TRACK_STREAMS, ID_LSB, ID_W, DEST_LSB and DEST_W. sv_emit.c renders finished parameters without protocol inference. Primitive defaults retain untracked behavior for manually instantiated older parameter sets.

Evidence: tests/native_axis_routing_test.py tracks packet origin per key and detects cross-origin corruption. Profiles cover ID-only/destination-only/combined interleaving, reopening keys, colliding IDs, absent identity fields, sidebands and stalls. One profile opens all 128 keys before any closes, exercising counter capacity. Reset profiles accept partial packets on identities absent from the next epoch; epoch bits also reject stale queued data. Child, standalone, generated and collection compositions pass. Oversized-tracking negatives preserve output; widened targets and absent LAST pass lint. tests/native_axis_fabric_test.py keeps identity constant within five-beat packets and retains its ten-beat fairness bound. Packet identity/interleaving follow Arm IHI 0051B §§2.7/4.1 ([specification](https://documentation-service.arm.com/static/64819f1516f0f201aa6b963c?token=)).
