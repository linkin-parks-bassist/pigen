---
status: "unverified"
created_at: "2026-09-13T15:03:15+10:00"
scope: "local"
source: "agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:23+10:00"
---

Use shared type syntax `type'(expression)`. Explicit conversion policy controls numerical-family and numerical/raw-vector boundaries. Identity casts validate but materialize no node; every required non-identity conversion remains explicit in runtime/constant semantic trees and later RTL.
