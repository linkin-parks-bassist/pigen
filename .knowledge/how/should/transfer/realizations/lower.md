---
status: "unverified"
created_at: "2026-09-13T15:03:18+10:00"
scope: "local"
source: "docs/superpowers/specs/2026-09-01-elastic-rtl-vertical-slice-design.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:22+10:00"
---

The adapter queries transfer descriptor realization, then realization descriptor capacity/ready/occupancy/reset. It dispatches on backend-neutral realization identity rather than source buf/port/fifo/skid enumeration. It creates payload/control endpoints and dedicated primitives. Data-type and expression adapters separately preserve layout and explicit conversion decisions.
