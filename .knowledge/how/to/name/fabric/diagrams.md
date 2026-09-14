---
status: "unverified"
created_at: "2026-09-13T15:03:14+10:00"
scope: "local"
source: "David's current-only knowledge instruction 2026-09-14; audited current contracts and inspected owner interfaces; Codex /root"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:48:47+10:00"
---

Identical source/options deterministically emit topology-derived SVG. One fabric defaults to OUTPUT.sv.svg; several use OUTPUT.sv.FABRIC.svg. --diagram PATH is available only for exactly one fabric; --no-diagram suppresses output. Diagram identifies instances, endpoints, routers/ports, physical/direct links and connections from the same topology used for RTL.

Layout is seeded from generated router tree, relaxed with topology-aware/angular springs and unit out-pull, footprint/padded collision separation, and crossing reduction that protects direct links most strongly. The SVG identifies all instances, signal endpoints, routers/ports, physical routed links, direct links and declared connections. These are deterministic presentation requirements; Topology/routing/reachability/manifests/RTL/SVG derive from one model, with verified forward and reverse routes. No private route/origin metadata enters child ports.
