---
status: "unverified"
created_at: "2026-09-13T15:04:51+10:00"
scope: "local"
source: "include/pigen/syntax.h"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:32+10:00"
---

Current pigen_syntax_kind includes compilation unit, module, parameter, typedef, unified signal declaration/declarator, clocked process, procedural block, if, nonblocking assignment and opaque. No structured pipeline/FSM/fabric node is present in this enum. Nodes retain location and parent/child/sibling IDs; tree borrows expanded source and owns nodes/shape/expression/type arenas.
