---
status: green
revised_at: "2026-11-17T18:00:32+11:00"
---

A fabric is an inline parent-module block connecting actual declared child ports:

```systemverilog
fabric commands begin
    ui.tx -> filter.commands;
    automation.tx -> filter.commands;
    filter.status > display.events;
endfabric
```

Both named and anonymous begin/endfabric blocks work. Each consuming output has one destination; several routed outputs can share an input. Types come from resolved ports, and normal assignment conversions are checked per edge. Producers and consumers expose only their own payload/valid/ready interfaces. Handles, source-recognition suffixes, exposed route words and the old top-level PAYLOAD_W dialect are rejected. Sender identity, if needed, belongs in packet DATA; use an explicit splitter child for one-to-many selection. what/are/fabric/connection/laws.md owns this contract.

src/resolve.c validates direction, payload compatibility, complete compatible handshake interfaces, scalar port shape, source/destination ownership and exclusive direct destinations across parent fabric blocks. Generated ordinary fabric blocks and unpacked whole-port shapes diagnose. Routed buffered children must use the parent's rising-edge clk and synchronous reset bindings. Explicit payload/control wires and omitted ports retain typed endpoint views (how/to/connect/child/ports.md).

src/fabric_lower.c coalesces the parent's fabric blocks into one resolved network. Routed connections use a balanced, pruned tree of three-port pigen_tree_router primitives. src/blocks.c:pigen_build_fabric_tree supplies the checked topology and forward/reverse route verification. Each source receives fixed internal route bits; source and sink endpoints have two-entry skids, and every router ingress has two-entry registered occupancy. Assignment conversion occurs before ingress packing. Mixed widths share an internal payload container sized to the widest routed destination; sinks unpack their own DATA width.

Routers advance one relative route bit per hop and arbitrate locally round-robin, retaining the selected beat during output stalls. Local arbitration does not promise equal bandwidth for all leaves. Shared FIFO head-of-line blocking remains possible; separate physical ports have separate endpoint queues. A plain > bypasses routed storage and remains exclusive. Ordinary static non-consuming wiring retains its existing fanout rules.

The same topology renders a deterministic SVG. One network defaults to OUTPUT.sv.svg; multiple parent networks use OUTPUT.sv.MODULE.svg. --diagram PATH requires exactly one network, and --no-diagram suppresses publication. The compile result owns SV and diagrams together; how/does/compilation/release/temporary/models.md owns lifetime details.

make fabric-test runs tests/inline_fabric_tree_test.py: shared consumers, thirteen-source/five-sink routing, mixed 16/33/129-bit DATA, signed/record/four-state payloads, widening, reset, stalls, independent ports, router stability/fairness/throughput, diagram checks and rejected legacy interfaces. examples/fabric_control.pigen is the complete public example. Native AXI whole-interface fabrics remain quarantined.
