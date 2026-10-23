---
status: green
revised_at: "2026-09-13T15:07:32+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Temporary analyzed nodes retain syntax/kind/literal kind/span/data type/shape/optional constant and typed children. Unary/binary/conditional nodes store full resolutions; conversions store decisions. The temporary arena owns nodes, child sequences, literal states and width constraints. Analysis may idempotently intern type/width catalogue entries but publishes no partial semantic expression or language object.
