---
status: "unverified"
created_at: "2026-09-13T15:03:08+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:30+10:00"
---

fifo is ordered depth-N storage with nonempty valid and registered-occupancy nonfull input readiness. It breaks combinational downstream-ready propagation. Simultaneous push/pop is sustained while nonfull; after a full queue pops, readiness returns on the next cycle from updated occupancy. Depth is a transfer argument independent of payload and shape.
