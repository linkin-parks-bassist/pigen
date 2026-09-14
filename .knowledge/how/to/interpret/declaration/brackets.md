---
status: "unverified"
created_at: "2026-09-13T15:03:08+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:25+10:00"
---

A colonless bracket in declaration-dimension position is count notation: [X] lowers structurally to [X-1:0]. On a type it sets packed width; after a name it sets array extent. Explicit colon-bearing ranges retain ordinary SV direction/meaning. Expression `value[3]` remains a select. Count and range shape forms stay structurally distinct even for equal element counts.
