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

port is a directly written payload register with a one-cycle valid pulse and ready=1. It does not retain or retry an unaccepted pulse. Internal port payload assignments are unconditional clocked writes for memory inference; the procedural guard qualifies next-cycle validity. Each port has one Pigen producer.
