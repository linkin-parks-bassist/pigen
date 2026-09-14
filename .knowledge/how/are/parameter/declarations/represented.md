---
status: "unverified"
created_at: "2026-09-14T20:39:58+10:00"
scope: "local"
source: "SPEC/PLAN/architecture/semantic invariants/Ari/editorial notes linear retirement audit; David knowledge-tree-only instruction 2026-09-14"
---

A structured parameter owns one symbol identity and one source expression occurrence for its value. Resolve parameter declarations in source order; later constants refer to the parameter symbol instead of substituting/copying initializer text or values. A resolved expression always has expression/type/shape/provenance/operands; constant identity is optional, never a precondition. Parameter-only expression trees point to canonical constant DAGs; runtime signals retain typed semantic expressions with invalid constant identity. Ordinary SV parameter literals/ranges use explicit SystemVerilog literal policy, not Pigen exact-literal policy. Canonical type aliases retain both typedef symbol and stored target ID; width/state/capability/projection queries follow that target without another lookup.
