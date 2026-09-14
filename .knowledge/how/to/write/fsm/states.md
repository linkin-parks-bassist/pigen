---
status: "unverified"
created_at: "2026-09-13T15:03:13+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:27+10:00"
---

The grammar is `state identifier : statement`. One statement follows the colon directly; multiple use ordinary begin/end. Actions run only when state active. goto is terminal on its syntactic path, changes state only under its explicit enclosing guard, and otherwise state holds. Overlapping transitions require structural exclusion.
