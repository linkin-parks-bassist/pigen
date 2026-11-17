---
status: green
revised_at: "2026-11-17T18:00:32+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An inline fabric is a parent-module item connecting declared output ports of child instances to declared input ports of other children. Named and anonymous begin/endfabric forms share semantics. Each connection resolves instance/port identities, infers its DATA types and checks normal assignment compatibility. Runtime sender identity is application DATA rather than fabric metadata. what/are/fabric/connection/laws.md owns endpoint and routing laws.

All fabric blocks in one parent share ownership checks and one source-routed topology. src/fabric_lower.c consumes the resolved child-connection records, builds a balanced three-port tree through src/blocks.c, inserts two-entry endpoint skids and generates pigen_tree_router instances. Private route words are packed at ingress and stripped at egress; child interfaces remain payload/valid/ready. Direct > links bypass the tree and have exclusive destinations. Non-consuming static links retain ordinary wiring behavior.

Omitted ports receive generated parent nets. Explicit payload/control bindings and typed endpoint views retain physical identity, coordinates and ownership. A consuming source cannot be reused by another fabric, named elastic child connection or local transfer. Destinations cannot have competing upstream/child/local writers or alias another elastic child input. Non-consuming peek remains legal. Direct exclusivity is checked across blocks and declaration order. Broader retained-SV driver auditing remains separate.

Routed buffered children require compatible parent clk/reset bindings. Static wiring and elastic handshakes require compatible complete interfaces. Generated ordinary fabric blocks diagnose. Scalar packed DATA supports normal width/signedness conversions and mixed packet widths; unpacked port shapes diagnose. Local arbitration preserves stalled grants and bounded progress in tested profiles, without promising uniform global bandwidth or absence of head-of-line blocking on shared FIFOs.

make fabric-test covers the topology, typed traffic, reset, stalls, independent ports, rejected legacy syntax and diagrams. Existing fabric-mixed-ownership-test, fabric-implicit-merge-test, fabric-cross-block-test, fabric-direct-order-error-test, fabric-endpoint-ownership-test and fabric-static-fanout-test retain their ownership and independent ordering witnesses. examples/fabric_control.pigen shows complete module hookup. Native AXI whole-interface fabrics are quarantined and rejected before publication.
