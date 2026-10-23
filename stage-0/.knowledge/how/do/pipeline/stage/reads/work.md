---
status: green
revised_at: "2026-09-13T15:07:17+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

Each stage field read sees its immutable incoming packet; <= computes outgoing fields visible only in the following stage. The first stage cannot read an uninitialized pipeline field and takes enclosing signals plus stage-local combinational values. Consuming external reads are atomic inputs: stage waits for all and consumes on advance under the complete enclosing guard.
