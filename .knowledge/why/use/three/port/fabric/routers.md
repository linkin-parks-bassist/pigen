---
status: green
revised_at: "2026-11-17T12:18:37+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The ordinary source-routed fabric generator builds a balanced, pruned tree of three-port routers, giving logarithmic router depth. Three-port describes router degree: excluding the incoming-port U-turn leaves two onward directions, selected by one relative route bit per hop. Routers rotate a compact internal route word instead of comparing global destination addresses. Route state stays inside the fabric; endpoint interfaces carry payload/valid/ready.

Small round-robin arbiters resolve contention locally, with two-entry registered input occupancy and buffered links propagating backpressure. This describes the parameter-only standalone fabric generator in src/blocks.c, including make_topology_parent, topology construction and route_connection. Inline ordinary fabrics currently lower to skid links and merges; native AXI fabrics use separate protocol-aware switches/arbiters (how/to/write/prototype/fabrics.md; how/are/native/axi/fabrics/connected/and/arbitrated.md).

make fabric-test passes routing, skid, router fairness, randomized delivery, deterministic SVG and direct-only checks (/tmp/pigen-fabric-readme-check.log). Low hop count and small local arbitration follow from the topology and router structure; device-specific area and timing require synthesis.
