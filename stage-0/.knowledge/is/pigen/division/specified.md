---
status: green
revised_at: "2026-09-13T15:07:27+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Source-visible Pigen numerical division/remainder remain fail-closed until runtime divide-by-zero behavior is specified. Ordinary SV division/remainder retain ordinary SV rules. src/data_type.c explicitly rejects DIVIDE/MODULO in concrete Pigen integer numerical resolution; operator syntax support alone does not mean the Pigen operation is admissible.
