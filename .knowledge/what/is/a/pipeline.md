---
status: green
revised_at: "2026-11-17T10:14:08+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

A pipeline describes elastic travelling-packet storage inside a parent clocked process. Its fixed field namespace expresses procedural-looking transformations across successive stages: the same x names a different physical stage register at each boundary. Every field has buf transfer type; omission is canonical and explicit buf is equivalent. Each stage reads immutable incoming fields and computes outgoing fields for the next stage, while yield exposes the final packet.

This is the language contract. Production currently rejects a later-stage x <= x + 1 because field versions share one semantic signal, as detailed in how/do/pipeline/stage/reads/work.md. Distinct-name examples exercise only partial pipeline support and must not be presented as evidence that the fixed-namespace contract works.
