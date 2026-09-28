---
status: green
revised_at: "2026-09-13T15:07:26+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Within reset, transfer the intended payload then validate it, e.g. `delay <= '0; validate(delay);`. Validity actions are synchronous next-state writes that compose with accepted transfers in source order: the later accepted action wins. validate alone preserves existing payload, so use it only when that payload is intentional.
