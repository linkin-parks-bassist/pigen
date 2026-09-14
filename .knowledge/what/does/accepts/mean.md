---
status: "unverified"
created_at: "2026-09-13T15:03:09+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:29+10:00"
---

`accepts(destination, source)` is exactly ready(destination) && valid(source). Both operands must be signal identifiers. valid and ready likewise accept one identifier. In a clocked process `if (destination <= source)` tests acceptance and performs that transfer on the same accepted edge; grouped conditions test all-member acceptance.
