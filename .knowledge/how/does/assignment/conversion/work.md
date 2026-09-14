---
status: "unverified"
created_at: "2026-09-13T15:03:15+10:00"
scope: "local"
source: "Full intrinsic-expression design/plan 2026-08-25 and semantic invariants; documentation audit Codex /root 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:38:04+10:00"
---

Resolve destination separately and ask its data-type family for one final conversion above the entire intrinsically resolved RHS. Same-family narrowing retains low bits and widening is exact without changing intermediate types; family/raw-vector changes need explicit casts. Transfer construction independently requires exact final data-type and shape identity, never merely convertible values.

The exact sequence is resolve destination lvalue/type/shape; intrinsically resolve complete RHS; request destination-family assignment conversion; append only final nonidentity node; require exact final type and shape; construct transfer. Same-family resize is quiet: narrowing keeps low bits, widening is exact; int<->uint and numerical<->raw need an explicit type'(expression) cast. C-style casts are not accepted. Conversion policy may gain real fixed-point scaling/rounding metadata without changing generic walker topology. pigen_resolve_assignment_value takes syntax/model/scope/expression/target/policy/error, and does not pass target into intrinsic analysis. Direct semantic transfer constructor must fail independently before allocating storage when final types/shapes differ. Tests assign int[17] to int[16]/int[24], reject implicit unsigned target and accept explicit unsigned cast.
