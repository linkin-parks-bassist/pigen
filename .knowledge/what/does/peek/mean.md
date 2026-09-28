---
status: green
revised_at: "2026-09-13T15:07:29+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

`peek(x)` requires one signal identifier and reads raw payload without adding validity, readiness or ownership. It may observe invalid/stale payload; guard it with valid/accepts when freshness matters. It binds the signal to the enclosing synchronous Pigen domain.
