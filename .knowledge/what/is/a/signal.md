---
status: "unverified"
created_at: "2026-09-13T15:03:07+10:00"
scope: "local"
source: "agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:30+10:00"
---

A runtime datum is a signal = data type × transfer type × declarator shape. Data type owns packed bits and interpretation; transfer type owns temporal validity, readiness, storage, consumption and production; shape owns ordered post-name array dimensions. Ordinary SV nets and variables share the signal arena. Backend nets, variables, queues and interfaces are realizations, not identities.

Related: [what is the declaration order](../the/declaration/order.md), [what are the transfer types](../../are/the/transfer/types.md).
