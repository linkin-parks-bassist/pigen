---
status: green
revised_at: "2026-10-21T18:29:17+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Public owner interfaces are under include/pigen/: source.h/lexer.h/preprocess.h for provenance; syntax.h/type_syntax.h/expression.h for syntax; data_type.h/integer.h/operation.h/transfer_type.h for type laws; semantic.h/predicate.h/expression_analysis.h/expression_resolve.h/expression_use.h/resolve.h for semantics; pipeline_identity.h for the pipeline identifier identity (the module-scope buf output packet identity of a parsed pipeline: the established signal, its bound symbol, the outgoing packet aggregate data type and the identifier span, with establish/accessor entry points, implemented); route.h for the retained-core structured route (one resolved module -> declarations -> clocked process -> direct atomic transfer: the whole-unit ready-graph node per direct atomic transfer, the route readiness record with sizing/signedness/aggregate check result and original-span anchor, and the init/free/build/check entry points with fail-loud stop). Implementations mirror these in src/. ids.h owns distinct identities.
