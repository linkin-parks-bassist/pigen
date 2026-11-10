---
status: green
revised_at: "2026-11-10T12:11:11+11:00"
---

Fabric declarations exist in two production forms.

Inline `fabric <name> begin ... endfabric` inside a module: each connection is `instance.port -> instance.port` (routed) or `instance.port > instance.port` (direct). Endpoints resolve through semantic child-instance records: the child module and port signal are checked, and the parent nets connected to the port and its `<port>_valid` / `<port>_ready` siblings are wired. Direct links are exclusive combinational equations. One routed link uses one `pigen_skid`; several routed links to the same destination merge with a priority arbiter (destination valid is the OR of sources, payload is a declaration-order conditional chain, each source ready is its grant gated by destination ready). The child instantiation itself remains verbatim standard SystemVerilog. Endpoint resolution requires explicit parent payload/valid/ready nets on the instance connections; generated boundary controls are mapped by their `<payload>_valid`/`<payload>_ready` names.

Standalone top-level `fabric` blocks are synthesized by the compiler driver through `pigen_lower_blocks` (src/blocks.c and the included src/fabric_svg.inc are production, not reference-only). They generate a module, router network, buffered hops, path metadata and deterministic SVG (`OUTPUT.sv.svg`, or `OUTPUT.sv.FABRIC.svg` for several). `--diagram PATH` is valid only for one fabric; `--no-diagram` suppresses SVG.

Limits: inline multi-source merge is priority, not fair round-robin. Standalone router arbitration and topology/SVG are implemented for the fabric block fixtures.
