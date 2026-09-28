---
status: green
revised_at: "2026-09-13T15:07:35+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

A consuming transfer cannot read one of its own buffered destinations, including through grouping/projections or across co-slice members: it would connect output-ready back to input-ready. Feedback needs a distinct ready-chain-breaking element such as register state, FIFO or skid. Indirect combinational ready cycles are also invalid.

Related: [how should ready cycles be validated](../../../../../how/should/ready/cycles/be/validated.md).
