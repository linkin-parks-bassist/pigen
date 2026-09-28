---
status: green
revised_at: "2026-09-13T15:07:30+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

port is a directly written payload register with a one-cycle valid pulse and ready=1. It does not retain or retry an unaccepted pulse. Internal port payload assignments are unconditional clocked writes for memory inference; the procedural guard qualifies next-cycle validity. Each port has one Pigen producer.
