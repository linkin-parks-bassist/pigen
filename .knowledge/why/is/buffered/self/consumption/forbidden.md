---
status: "unverified"
created_at: "2026-09-13T15:03:11+10:00"
scope: "local"
source: "SPEC.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:35+10:00"
---

A consuming transfer cannot read one of its own buffered destinations, including through grouping/projections or across co-slice members: it would connect output-ready back to input-ready. Feedback needs a distinct ready-chain-breaking element such as register state, FIFO or skid. Indirect combinational ready cycles are also invalid.

Related: [how should ready cycles be validated](../../../../../how/should/ready/cycles/be/validated.md).
