---
status: "unverified"
created_at: "2026-09-13T15:03:10+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:26+10:00"
---

Within reset, transfer the intended payload then validate it, e.g. `delay <= '0; validate(delay);`. Validity actions are synchronous next-state writes that compose with accepted transfers in source order: the later accepted action wins. validate alone preserves existing payload, so use it only when that payload is intentional.
