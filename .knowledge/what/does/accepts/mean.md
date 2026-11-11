---
status: green
revised_at: "2026-11-11T19:58:57+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

`accepts(destination, source)` is exactly ready(destination) && valid(source). Both operands must be signal identifiers. valid and ready likewise accept one identifier. `when (destination <= source) statement` tests acceptance, performs that transfer and runs the body on the same fired edge; an `if` condition is computed, so `if (a <= b)` is a relational comparison. Grouped conditions test all-member acceptance.
