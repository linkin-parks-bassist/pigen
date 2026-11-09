---
status: green
revised_at: "2026-11-10T08:38:15+11:00"
---

The structured compiler accepts an inline `fabric <name> begin ... endfabric` block inside a module. Each connection is `instance.port -> instance.port` (routed) or `instance.port > instance.port` (direct). Endpoints resolve through semantic child-instance records: the child module and port signal are checked, and the parent nets connected to the port and its `<port>_valid` / `<port>_ready` siblings are wired with RTL equations (destination payload = source payload, destination valid = source valid, source ready = destination ready). The child instantiation itself remains verbatim standard SystemVerilog.

Current limits: routed and direct links are both combinational wiring; there is no balanced three-port router network, no buffered ready break, no round-robin arbitration when several routed sources target one input, and no SVG/topology manifest. Endpoint resolution requires explicit parent payload/valid/ready nets on the instance connections; generated boundary controls are mapped by their `<payload>_valid`/`<payload>_ready` names. The quarantined prototype sources (src/blocks.c, src/fabric_svg.inc) remain reference material only and are not called by production.
