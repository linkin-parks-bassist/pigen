---
status: green
revised_at: "2026-11-10T08:44:16+11:00"
---

The structured compiler accepts an inline `fabric <name> begin ... endfabric` block inside a module. Each connection is `instance.port -> instance.port` (routed) or `instance.port > instance.port` (direct). Endpoints resolve through semantic child-instance records: the child module and port signal are checked, and the parent nets connected to the port and its `<port>_valid` / `<port>_ready` siblings are wired. Direct links are exclusive combinational equations. Routed links instantiate one `pigen_skid` between the endpoint nets, so the ready path is broken and one item holds under stall. The child instantiation itself remains verbatim standard SystemVerilog.

Current limits: routed links are one skid stage, not a balanced three-port router network; there is no round-robin arbitration when several routed sources target one input (multiple sources would multi-drive), and no SVG/topology manifest. Endpoint resolution requires explicit parent payload/valid/ready nets on the instance connections; generated boundary controls are mapped by their `<payload>_valid`/`<payload>_ready` names. The quarantined prototype sources (src/blocks.c, src/fabric_svg.inc) remain reference material only and are not called by production.
