---
status: green
revised_at: "2026-09-13T15:07:12+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Yes. An unpeeked payload select or member read projects bits without creating a smaller signal. `out <= packet[7:0]` consumes the complete packet. All projections in one transfer deduplicate by base signal identity; two independently guarded statements require proof of mutual exclusion.
