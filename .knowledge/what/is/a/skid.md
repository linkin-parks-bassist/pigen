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

skid is exactly two-entry storage with nonempty valid and registered-occupancy nonfull readiness. It absorbs delayed backpressure and deliberately breaks ready chains. It supports simultaneous push/pop while nonfull; a full queue becomes input-ready on the following cycle after a pop.
