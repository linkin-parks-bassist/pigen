---
status: green
revised_at: "2026-09-14T20:35:24+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

validate forces a locally owned stored output valid next cycle; invalidate forces invalid; flush empties every buffered item. A validate without transfer preserves existing payload and is safe only when that payload is intentional. FIFO/skid validation forces nonempty state, adding one head item when previously empty; invalidation forces the output invalid. These are synchronous next-state writes without waiting for downstream readiness and compose in source order like nonblocking assignments: a later accepted transfer overrides an earlier action and a later action overrides an earlier transfer. Reset seeding may use `delay <= '0; validate(delay);` to set payload and validity together. Actions apply only to locally owned buf/fifo/skid/port. Inputs cannot be changed because producer owns validity. Always-valid wire/reg/logic validate warns and does nothing; invalidate/flush diagnose.
