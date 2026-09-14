---
status: "unverified"
created_at: "2026-09-13T15:03:12+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:17+10:00"
---

Each stage field read sees its immutable incoming packet; <= computes outgoing fields visible only in the following stage. The first stage cannot read an uninitialized pipeline field and takes enclosing signals plus stage-local combinational values. Consuming external reads are atomic inputs: stage waits for all and consumes on advance under the complete enclosing guard.
