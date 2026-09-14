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

Resolution is intrinsic with no expected result type. Analysis builds a temporary typed tree and appends no semantic expressions/lvalues. One postorder materializer emits children, explicit non-identity conversions, then operations. On failure no partial semantic tree survives. Runtime and constant conversion topology is isomorphic.

Analysis is bottom-up, does ordinary idempotent type/width interning but appends no semantic expressions/lvalues/language objects. It retains syntax/kind/provenance/shape/intrinsic type or exact value/optional constant and complete decisions. Recursive symbol lookup, literal parsing, operator mapping and shape/type checks belong to expression_analysis.c; public orchestration and one postorder materializer belong to expression_resolve.c. pigen_analyze_expression and pigen_free_analyzed_expr_arena expose the temporary owner; current policy inputs must follow current headers. Cast analysis calls the shared type resolver and explicit conversion policy. Materialization emits children, each required nonidentity conversion, then operation with exact effective child types. Identity casts validate then disappear. Failed analysis frees temporary state; failed construction reports its own error and does not retry another path. Tests cover int[17] root over int[9] addition, exact literal beneath its representation conversion, lossless mixed promotion, explicit casts and runtime/constant topology. Differently typed assignments must leave the same intrinsic RHS tree. Invalid raw/numerical/cast forms append no partial expressions and diagnose operator/cast spans. Source-visible division/modulo remain fail-closed pending divide-by-zero semantics; ordinary SV keeps its own laws.
