---
status: green
revised_at: "2026-11-17T15:48:03+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An ordinary consuming scalar output has one destination. `>` is exclusive direct linkage: neither its source nor destination may be reused by another consuming connection. One or more dashes before > denotes routing; several routed sources may target one destination and arbitrate. Dash count is retained as tier for future policy; current arbitration is round-robin. Static scalar fanout remains legal under its non-consuming rules.

Standalone source-routed fabric destinations are instance.port, with an optional source-recognition label. That suffix does not create another consumer port; how/to/write/prototype/fabrics.md owns the parser/emission behavior and its unresolved design-authority status.

Native AXI4-Stream, AXI4-Lite and full AXI4 whole-interface fabrics are quarantined and not in use. Their compiler semantic publication and RTL lowering are disconnected. Retained TDEST switching, address-window routing, ID provenance and transaction-ownership designs are documented as inactive work by how/are/native/axi/fabrics/connected/and/arbitrated.md. Ordinary scalar fabric laws remain active; native module boundaries, child bindings and explicit channel transfers remain active.
