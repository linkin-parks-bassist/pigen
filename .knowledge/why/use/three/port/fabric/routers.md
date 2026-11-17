---
status: green
revised_at: "2026-11-17T17:41:59+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

A balanced, pruned tree gives logarithmic router depth. A three-port router excludes the incoming-port U-turn, leaving two onward directions selected by one relative route bit. The router advances a compact internal route word instead of comparing global destination addresses.

Production inline fabrics use this architecture. src/blocks.c:pigen_build_fabric_tree builds and verifies the topology from resolved physical port identities; src/fabric_lower.c emits parent-owned RTL and rtl/pigen_primitives.sv:pigen_tree_router implements the node. Source/sink skids and two-entry router ingress queues cut combinational ready propagation. Per-output round-robin grants remain stable during stalls.

Route metadata never enters child-module interfaces. Port-to-port hookup, typing, ownership, clock/reset rules and direct bypasses are owned by how/to/write/prototype/fabrics.md. Local round-robin service can give unequal bandwidth to different leaves. Shared FIFO head-of-line blocking is a property of the lightweight tree; independent endpoint queues do not make shared links nonblocking.

tests/fabric_tree_router_tb.sv checks late-contender stalled stability, route rotation, FIFO order, local fairness and saturated throughput. tests/inline_fabric_tree_test.py scores independent end-to-end traffic, mixed widths, shared consumers, reset and direct paths, and validates the deterministic SVG. Device-specific area/timing still requires synthesis. Native AXI interconnect generation remains quarantined.
