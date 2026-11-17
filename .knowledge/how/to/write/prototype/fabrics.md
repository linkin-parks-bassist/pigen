---
status: green
revised_at: "2026-11-17T12:18:38+11:00"
---

Native AXI whole-bus links accept parent/child endpoints, including indexed named generate paths (how/are/named/generate/scope/paths/resolved.md), with Stream TDEST switching, memory-mapped `at [BASE:LIMIT]` address routing and native arbitration, separately from scalar routing; how/are/native/axi/fabrics/connected/and/arbitrated.md owns their contracts, tests and memory-mapped transaction profile.

Fabric declarations exist in three production forms.

Inline `fabric <name> begin ... endfabric` or anonymous `fabric begin ... endfabric` inside a module: each connection is `instance.port -> instance.port` (routed) or `instance.port > instance.port` (direct). Endpoints resolve through semantic child-instance records, unwrapping connection conversions to the parent payload net; child module and port signals are checked, and parent nets connected to the port and its `<port>_valid` / `<port>_ready` siblings are wired. Direct links are exclusive combinational equations. One routed link uses one `pigen_skid`. Two routed sources to one destination use registered `pigen_merge2`; three or more use `pigen_merge` with packed valid/ready/payload vectors and a rotating last-grant index. Both are round-robin, so continuous contention alternates. Omitted child ports can use generated parent endpoint nets; explicit connections remain supported. Boundary controls are recovered from typed output views, semantic endpoints or explicit sibling connections for single links and merge sources. Packed ready indices match each source's payload/valid lane. See how/to/connect/child/ports.md for endpoint syntax and remaining coverage.

Typed standalone `fabric name #(parameters) (typed ports) begin ... endfabric` declarations use structured module semantics and native protocol contracts; how/to/write/typed/standalone/fabrics.md owns their syntax, specialization and verification. Their connection bodies use the same rules as inline native fabrics.

Legacy parameter-only top-level `fabric` blocks are synthesized by the compiler driver through `pigen_lower_blocks` (src/blocks.c and src/fabric_svg.inc are production). They generate a module, a balanced, pruned tree of three-port routers with logarithmic hop counts, one relative route bit per hop, and two-entry registered input occupancy, round-robin grants, buffered skid hops, path metadata and deterministic SVG (`OUTPUT.sv.svg`, or `OUTPUT.sv.FABRIC.svg` for several). `--diagram PATH` is valid only for one fabric; `--no-diagram` suppresses SVG. `fabric_router_fair_tb`, `fabric_merge_fair_tb` and `fabric_merge3_tb` hold all competing sources continuously valid under backpressure and require balanced service.

Ordinary inline routed groups are limited to 32 sources and grouped by destination spelling across all parent fabric blocks. Direct destinations are exclusive across blocks and declaration order. Broader aliases and retained ordinary-SV driver ownership remain unaudited; see what/is/an/inline/fabric.md. make fabric-implicit-merge-test checks ordered delivery under stalls for omitted ports and payload aliases.
