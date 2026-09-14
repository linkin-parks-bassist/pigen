---
status: "unverified"
created_at: "2026-09-14T20:35:24+10:00"
scope: "local"
source: "USER_GUIDE.md Declare a fabric; docs/fabric.pigen; README fabric figure; retirement audit Codex /root 2026-09-14"
---

Production prototype fabrics are top-level fixed-width units, e.g. `fabric command_network #(parameter integer PAYLOAD_W = 32) begin boot.tx.command > controller.rx; cpu.tx.command -> controller.rx.cpu; debugger.tx.command --> controller.rx.debugger; endfabric`. They emit flattened ready/valid boundaries, route constants and recognized-source constants. A direct exclusive link has its own two-entry endpoint queue; routed links use buffered three-port routers. This is historical executable prototype behavior, distinct from the target inline module-owned, two-component instance.port, per-connection inferred-type fabric. Delete its parser and lowering at the complete fabric cutover; do not maintain two dialects.

The retired docs/fabric.pigen and its generated SVG illustrated this prototype network, not target child-instance resolution. Topology, route/reachability, endpoint/port/router/link labels, deterministic layout and diagram naming have their own owners. Example topology edges are captured in `what/does/the/retired/fabric/example/illustrate.md`.
