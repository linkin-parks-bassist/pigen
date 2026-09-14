---
status: "unverified"
created_at: "2026-09-13T15:04:59+10:00"
scope: "local"
source: "rtl/pigen_primitives.sv pigen_skid"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:19+10:00"
---

pigen_skid uses one-bit read/write pointers, two payload slots and two-bit occupancy. Push toggles write, pop toggles read; exactly-one push/pop adjusts count, both/neither hold. in_ready is count!=2 independently of out_ready. Reset/clear empty state unless explicit valid seeding applies.
