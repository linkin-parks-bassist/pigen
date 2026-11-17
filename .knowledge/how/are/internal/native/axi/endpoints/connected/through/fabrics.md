---
status: green
revised_at: "2026-11-17T12:35:10+11:00"
---

Native AXI endpoints cannot currently be connected through fabric declarations. Both direct and routed whole-interface fabric links are quarantined and rejected before semantic edge publication. The retained design is owned by how/are/native/axi/fabrics/connected/and/arbitrated.md.

Module ports, whole-endpoint child bindings and explicit channel transfers remain available. An explicit buffering child can declare native input/output ports; parent logic accesses child.bus channels or binds compatible module boundaries. Standalone internal AXI declarations remain forbidden. how/are/internal/native/axi/child/bindings/resolved.md owns this active model.
