---
status: "unverified"
created_at: "2026-09-13T15:03:13+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:35+10:00"
---

Public endpoint interfaces carry payload, valid and ready only: no peer names, selectors, routes, address or origin metadata. Fabric does not interpret payload addressing. One-to-many needs an explicit splitter child which interprets protocol fields and drives one distinct output for unicast; each output is fabric-connected once.
