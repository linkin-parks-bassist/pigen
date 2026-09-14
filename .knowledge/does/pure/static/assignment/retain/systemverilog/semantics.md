---
status: "unverified"
created_at: "2026-09-13T15:03:10+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:13+10:00"
---

Yes. A purely static statement containing no buffered signal retains ordinary procedural behavior, including source-ordered nonblocking writes to the same variable. When mixed with buffered participants, static destinations are always-ready members and update only on the common fire event. wire remains an invalid procedural destination.
