---
status: green
revised_at: "2026-11-09T15:23:37+11:00"
---

A fabric implementation remains in the quarantined textual prototype, not in the production structured compiler. src/blocks.c contains parsing, connection checks, balanced topology construction, route calculation and SystemVerilog emission, including two-entry endpoint queues and buffered three-port routers. src/fabric_svg.inc renders topology diagrams. tests/fabric_smoke.sh and its fabric fixtures/testbenches exercise this old route, but the current CLI diagnoses the prototype corpus as migration debt.

The prototype syntax is top-level and fixed-width, for example `fabric command_network #(parameter integer PAYLOAD_W = 32) begin boot.tx.command > controller.rx; cpu.tx.command -> controller.rx.cpu; debugger.tx.command --> controller.rx.debugger; endfabric`. Its emitter produces flattened ready/valid boundaries, route constants and recognized-source constants.

The specified replacement is an inline, parent-module-owned fabric resolving two-component instance.port identities and checking types per connection. It remains an architecture migration gate. Production src/pigen.c calls pigen_compile_source; no prototype fallback is approved. Remove the old fabric parser and lowering when the structured fabric path covers the spec. Topology, routing, labels, layout and diagram naming have focused owners.
