---
status: green
revised_at: "2026-11-11T15:43:53+11:00"
---

Fabric declarations exist in two production forms.

Inline `fabric <name> begin ... endfabric` or anonymous `fabric begin ... endfabric` inside a module: each connection is `instance.port -> instance.port` (routed) or `instance.port > instance.port` (direct). Endpoints resolve through semantic child-instance records, unwrapping connection conversions to the parent payload net; child module and port signals are checked, and parent nets connected to the port and its `<port>_valid` / `<port>_ready` siblings are wired. Direct links are exclusive combinational equations. One routed link uses one `pigen_skid`. Two routed sources to one destination use registered `pigen_merge2`; three or more use `pigen_merge` with packed valid/ready/payload vectors and a rotating last-grant index. Both are round-robin, so continuous contention alternates. Endpoint resolution requires explicit parent payload/valid/ready nets on instance connections; generated boundary controls are mapped by their `<payload>_valid`/`<payload>_ready` names.

Standalone top-level `fabric` blocks are synthesized by the compiler driver through `pigen_lower_blocks` (src/blocks.c and src/fabric_svg.inc are production). They generate a module, balanced three-port routers with two-entry registered input occupancy, round-robin grants, buffered skid hops, path metadata and deterministic SVG (`OUTPUT.sv.svg`, or `OUTPUT.sv.FABRIC.svg` for several). `--diagram PATH` is valid only for one fabric; `--no-diagram` suppresses SVG. `fabric_router_fair_tb`, `fabric_merge_fair_tb` and `fabric_merge3_tb` hold all competing sources continuously valid under backpressure and require balanced service.

Limits: none known for the current fabric fixtures.
