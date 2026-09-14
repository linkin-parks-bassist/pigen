---
status: "unverified"
created_at: "2026-09-13T15:03:09+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:23+10:00"
---

`_ <= source;` accepts and drops one source token without storage. `_` is an always-ready member in co-slices, e.g. `{result, _} <= {computed, old_token};`, retiring the unwanted source on the same atomic event as real destinations.
