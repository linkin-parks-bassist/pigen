---
status: green
revised_at: "2026-11-09T21:26:24+11:00"
---

Fabric is not implemented on the structured production path. The quarantined prototype sources (src/blocks.c, src/fabric_svg.inc) still contain top-level fixed-width fabric parsing, balanced topology construction, route calculation, router/skid emission and SVG rendering, but the production `pigen` CLI does not link or call them. A transfer-first `fabric ... endfabric` input is not accepted by the structured parser, and the structured language has no child-instance syntax or resolved instance-port types yet.

The specified fabric is inline and parent-module-owned: two-component instance.port endpoints, per-connection payload width/signedness checks, exclusive `>` links, routed `->`/`-->` links with round-robin arbitration, balanced three-port relative routing, buffered ready breaks, and one model producing both RTL and SVG. Building it requires child-instance resolution first (what/is/the/plan.md). The prototype is reference material only; it is not a behavioral authority.
