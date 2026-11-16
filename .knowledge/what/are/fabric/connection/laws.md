---
status: green
revised_at: "2026-11-15T12:09:25+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

An ordinary consuming scalar output has one destination. `>` is exclusive direct linkage: neither its source nor destination may be reused by another consuming connection. One or more dashes before > denotes routing; several routed sources may target one destination and arbitrate. Dash count is retained as tier for future policy; current arbitration is round-robin.

Native AXI-Stream sources may have several distinct routed destinations when TDEST has enough encoding space. Source-local link order maps destination values 0, 1, … to outgoing links. Switching composes with per-destination arbitration and preserves complete beat records. Switched merges require target TID provenance space and arbitrate per accepted beat (how/are/native/stream/fabric/identities/preserved.md). Direct/native ownership, clock and contract checks still apply; duplicates and competing local/child consumers reject. The limit is 32 outgoing destinations per source. how/does/the/native/axi/stream/destination/switch/work.md owns the mechanism and independent standard-pin tests.

AXI4-Lite and AXI4 sources may route to several distinct destinations using `s -> m at [BASE:LIMIT];`. Every outgoing link of a shared source needs an inclusive constant window, fitting its address width and not overlapping another source-local window. Window annotations require routed memory-mapped native endpoints. A single annotated link also decodes its range. Five-channel switching composes with target arbitration, preserving independent write/read ownership and full AXI ID provenance. Unmapped transactions return DECERR. how/does/native/memory/mapped/address/routing/work.md owns bounds, mechanism and verification; how/are/native/axi/fabrics/connected/and/arbitrated.md owns shared native contracts. Static scalar fanout remains legal under its existing non-consuming rules.
