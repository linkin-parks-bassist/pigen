---
status: green
revised_at: "2026-09-13T15:07:22+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

The adapter queries transfer descriptor realization, then realization descriptor capacity/ready/occupancy/reset. It dispatches on backend-neutral realization identity rather than source buf/port/fifo/skid enumeration. It creates payload/control endpoints and dedicated primitives. Data-type and expression adapters separately preserve layout and explicit conversion decisions.
