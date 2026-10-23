---
status: green
revised_at: "2026-09-13T15:07:29+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

`accepts(destination, source)` is exactly ready(destination) && valid(source). Both operands must be signal identifiers. valid and ready likewise accept one identifier. In a clocked process `if (destination <= source)` tests acceptance and performs that transfer on the same accepted edge; grouped conditions test all-member acceptance.
