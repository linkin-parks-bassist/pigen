---
status: green
revised_at: "2026-11-11T17:01:55+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Each lvalue occurrence has stable identity and projected expression/type. Projection owns its base symbol/optional signal; concat owns ordered recursively nested lvalue children, all resolved before publication. Direct symbols, groups, packed indexing/ranges/indexed selects/concat work; parameters/operators cannot be destinations. Expressions have direct optional lvalue backreferences; no name/arena scan reconstructs them.

Lvalue shape alone does not establish structural writability. pigen_lvalue_is_writable rejects missing/placeholder lvalues and projections rooted in input signals; a concatenation requires every child to be writable. This direction check is shared by explicit child-control ownership validation and endpoint lowering. Scalar and selected parent-input ready/valid bindings remain observations, while conflicting automatic handshake owners diagnose. The child-ready-manual, child-valid-manual and child-slice-ownership suites exercise these boundaries. Transfer-specific whole-token and ownership constraints remain separate.
