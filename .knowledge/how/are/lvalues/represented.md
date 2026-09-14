---
status: "unverified"
created_at: "2026-09-13T15:03:16+10:00"
scope: "local"
source: "agent_notes/COMPILER_ARCHITECTURE.md; agent_notes/SEMANTIC_INVARIANTS.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:15+10:00"
---

Each lvalue occurrence has stable identity and projected expression/type. Projection owns assignable base symbol/optional signal; concat owns ordered recursively nested lvalue children, all resolved before publication. Direct symbols, groups, packed indexing/ranges/indexed selects/concat work; parameters/operators cannot be destinations. Expressions have direct optional lvalue backreferences; no name/arena scan reconstructs them.
