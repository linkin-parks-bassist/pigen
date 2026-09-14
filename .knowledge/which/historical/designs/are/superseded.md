---
status: "unverified"
created_at: "2026-09-13T15:05:00+10:00"
scope: "local"
source: "2026-08-25 data-type-policy design; 2026-08-25 intrinsic expression design; 2026-08-27 data-first design; PLAN.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:35+10:00"
---

The 2026-08-25 data-type-policy design proposed expected-result sizing, cast-required mixed int/uint and Pigen byte. The later intrinsic expression design removes destination-context arithmetic and permits lossless mixed promotion; 2026-08-27 declarations deletes Pigen byte. Current SPEC/PLAN/semantic invariants win. Historical examples of byte casts or expected-result APIs are evidence of past decisions, not current implementation tasks.

Related: [what is the declaration order](../../../../what/is/the/declaration/order.md), [what are the intrinsic arithmetic width examples](../../../../what/are/the/intrinsic/arithmetic/width/examples.md).
