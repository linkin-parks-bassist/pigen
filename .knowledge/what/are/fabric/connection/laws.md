---
status: green
revised_at: "2026-11-17T18:00:31+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An ordinary consuming scalar output has one destination. `>` is exclusive direct linkage: neither its source nor destination may be reused by another consuming connection. One or more dashes before > denotes routing; several routed sources may target one destination and arbitrate. Dash count is retained as tier for future policy; current arbitration is round-robin. Static scalar fanout remains legal under its non-consuming rules.

The governing SPEC.md at d14aef2, Fabric blocks, requires instance.output_port -> instance.input_port connections between actual declared top-level child-module ports. No source handles, recognition suffixes, route selectors or origin metadata belong in that interface. Producers need no knowledge of consumers. Several sources may arbitrate into one physical input. One-to-many selection belongs in an explicit splitter child with distinct output ports, each connected once. Sender identity, when required by an application, is ordinary packet DATA. Avoid handle-like aliases that imply independent endpoint queues while sharing physical-port readiness. Legacy standalone label/path syntax is rejected; how/to/write/prototype/fabrics.md owns the checked implementation.

Native AXI4-Stream, AXI4-Lite and full AXI4 whole-interface fabrics are quarantined and not in use. Their compiler semantic publication and RTL lowering are disconnected. Retained TDEST switching, address-window routing, ID provenance and transaction-ownership designs are documented as inactive work by how/are/native/axi/fabrics/connected/and/arbitrated.md. Ordinary scalar fabric laws remain active; native module boundaries, child bindings and explicit channel transfers remain active.

Ordinary inline routing is implemented by the checked parent-owned three-port tree. Complete scalar packed DATA and normal assignment conversions are supported; generated ordinary fabric blocks and unpacked whole-port shapes diagnose. Static wiring and elastic handshakes must have compatible complete interfaces. Mechanism, local arbitration limits and tests: how/to/write/prototype/fabrics.md.
