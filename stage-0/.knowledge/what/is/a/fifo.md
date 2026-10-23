---
status: green
revised_at: "2026-09-13T15:07:30+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

fifo is ordered depth-N storage with nonempty valid and registered-occupancy nonfull input readiness. It breaks combinational downstream-ready propagation. Simultaneous push/pop is sustained while nonfull; after a full queue pops, readiness returns on the next cycle from updated occupancy. Depth is a transfer argument independent of payload and shape.
