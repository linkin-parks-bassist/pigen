---
status: green
revised_at: "2026-09-13T15:07:13+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Yes. A purely static statement containing no buffered signal retains ordinary procedural behavior, including source-ordered nonblocking writes to the same variable. When mixed with buffered participants, static destinations are always-ready members and update only on the common fire event. wire remains an invalid procedural destination.
