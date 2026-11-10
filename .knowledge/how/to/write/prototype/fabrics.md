---
status: green
revised_at: "2026-11-10T13:45:03+11:00"
---

Fabric declarations exist in two production forms.

Inline `fabric <name> begin ... endfabric` inside a module: each connection is `instance.port -> instance.port` (routed) or `instance.port > instance.port` (direct). Endpoints resolve through semantic child-instance records: the child module and port signal are checked, and the parent nets connected to the port and its `<port>_valid` / `<port>_ready` siblings are wired. Direct links are exclusive combinational equations. One routed link uses one `pigen_skid`; several routed links to the same destination merge with a fixed declaration-order priority arbiter (destination valid is the OR of sources, payload is a conditional chain, each source ready is its grant gated by destination ready). Endpoint resolution requires explicit parent payload/valid/ready nets on the instance connections; generated boundary controls are mapped by their `<payload>_valid`/`<payload>_ready` names.

Standalone top-level `fabric` blocks are synthesized by the compiler driver through `pigen_lower_blocks` (src/blocks.c and src/fabric_svg.inc are production). They generate a module, balanced three-port routers with two-entry registered input occupancy, round-robin grants, buffered skid hops, path metadata and deterministic SVG (`OUTPUT.sv.svg`, or `OUTPUT.sv.FABRIC.svg` for several). `--diagram PATH` is valid only for one fabric; `--no-diagram` suppresses SVG. `fabric_router_fair_tb` holds both competing sources continuously valid under output backpressure and requires both to be served with near-equal counts, distinguishing round-robin from fixed priority.

Limits: inline multi-source merge is fixed priority, not fair round-robin; a continuous-contention inline test and fair grants are still required.
