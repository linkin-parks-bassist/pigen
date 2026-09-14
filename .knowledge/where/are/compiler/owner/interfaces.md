---
status: "unverified"
created_at: "2026-09-13T15:03:19+10:00"
scope: "local"
source: "repository inventory; agent_notes/COMPILER_ARCHITECTURE.md"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-13T15:07:34+10:00"
---

Public owner interfaces are under include/pigen/: source.h/lexer.h/preprocess.h for provenance; syntax.h/type_syntax.h/expression.h for syntax; data_type.h/integer.h/operation.h/transfer_type.h for type laws; semantic.h/predicate.h/expression_analysis.h/expression_resolve.h/expression_use.h/resolve.h for semantics. Implementations mirror these in src/. ids.h owns distinct identities.
