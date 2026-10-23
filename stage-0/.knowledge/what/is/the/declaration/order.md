---
status: green
revised_at: "2026-09-13T15:07:32+10:00"
checked_at: '2026-09-13T15:07:12+10:00'
---

The sole intended Pigen order is data-type transfer-type declarator, for example `int[16] fifo[8] pending[lanes];`. Here 16 is signed payload width, 8 is descriptor-owned FIFO depth, and lanes is post-name shape. The production prototype still uses transfer-first syntax; that implementation lag grants no compatibility status.

Related: [how does declaration syntax store omission](../../../../how/does/declaration/syntax/store/omission.md), [who owns omitted transfer policy](../../../../who/owns/omitted/transfer/policy.md).
