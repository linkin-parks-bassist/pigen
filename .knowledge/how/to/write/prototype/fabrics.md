---
status: green
revised_at: "2026-11-10T13:56:04+11:00"
---

Fabric declarations exist in two production forms.

Inline `fabric <name> begin ... endfabric` inside a module: each connection is `instance.port -> instance.port` (routed) or `instance.port > instance.port` (direct). Endpoints resolve through semantic child-instance records, unwrapping connection conversions to the parent payload net; child module and port signals are checked, and parent nets connected to the port and its `<port>_valid` / `<port>_ready` siblings are wired. Direct links are exclusive combinational equations. One routed link uses one `pigen_skid`. Two routed sources to one destination use a registered `pigen_merge2` primitive with a last-grant bit, so continuous contention alternates; three or more still use the fixed declaration-order priority equation chain. Endpoint resolution requires explicit parent payload/valid/ready nets on instance connections; generated boundary controls are mapped by their `<payload>_valid`/`<payload>_ready` names.

Standalone top-level `fabric` blocks are synthesized by the compiler driver through `pigen_lower_blocks` (src/blocks.c and src/fabric_svg.inc are production). They generate a module, balanced three-port routers with two-entry registered input occupancy, round-robin grants, buffered skid hops, path metadata and deterministic SVG (`OUTPUT.sv.svg`, or `OUTPUT.sv.FABRIC.svg` for several). `--diagram PATH` is valid only for one fabric; `--no-diagram` suppresses SVG. `fabric_router_fair_tb` requires both continuously-valid router sources to be served with near-equal counts. `fabric_merge_fair_tb` does the same for the inline two-source merge.

Limits: inline multi-source merge is fair only for two sources; three or more are still fixed priority.
