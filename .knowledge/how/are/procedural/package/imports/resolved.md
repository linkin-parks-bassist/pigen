---
status: green
revised_at: "2026-11-16T20:54:08+11:00"
---

Structured combinational and clocked `begin/end` blocks accept explicit and wildcard imports from structured packages. Imports are declarations and precede executable statements. They bind original package constants/types in the block's lexical scope, without creating storage. Common namespace rules and package eligibility: how/are/native/axi/package/imports/resolved.md.

src/syntax.c:parse_procedural_statement reuses parse_import only for lexical block items. It saves/restores opaque_byte_cursor and starts the import probe at its own token, preventing surrounding module text from becoming a procedural OPAQUE child. Imports with native protocol-type candidates select structured module-root always_comb lowering. Ordinary-only imports preserve ordinary procedures; selected structured procedures resolve ordinary companions (how/are/ordinary/package/dependencies/resolved.md). Unsupported mixed packages still diagnose.

src/resolve.c resolves combinational imports in the existing procedural scope. They count as declarations and add no executable child. Clocked blocks containing imports create a procedural child scope, resolve entries/statements in order and restore the parent on success or failure. Other clocked block behavior retains its existing scope path. Nested imports do not leak into parents or siblings. Explicit repeats/conflicts and lazy wildcard activation use the shared semantic import API.

Module declaration collection prebinds genuine outer import uses before later declarations (how/are/native/axi/package/imports/resolved.md). Its syntax barriers also recognize preceding block imports: an explicit name, or an exported wildcard candidate, prevents that use from prematurely activating an enclosing module import. Qualified package lookups remain independent. Actual procedural resolution determines local precedence and ambiguity.

src/output.c suppresses procedural IMPORT items rather than hoisting them into module scope. Typed lowering supplies canonical values/types for expressions, conversions, native records and channel fields. Structured lowering emits no raw procedural import or imported storage object.

Evidence: tests/native_protocol_test.py:package_import_tests checks 26 independent value simulations with Icarus and fatal Verilator lint, plus 21 rejection/output-preservation cases. They cover nested/explicit/wildcard/repeated imports, unused candidates shadowed by locals, scope isolation, imported types/size casts and declaration order. Six all-family procedural_import/procedural_import_type transport profiles independently score all channel pins, FIFO/skid/buf capacity, stalls, queued reset and interface isolation, and assert imports are absent from emitted module scope. examples/native_axi_packages.pigen imports packed word_t inside the Stream relay's clocked block; all three tops lint.

Remaining: class/enum/union and broader mixed-package typing, forward and other retained procedural namespaces, clocked local-declaration/hierarchy integration and typed subroutine evaluation. Ordinary package functions/tasks coexist with typed DATA/contracts and remain callable in preserved SV (how/are/retained/package/subroutines/represented.md); their scoped body imports/references have a separate owner (how/are/retained/package/namespace/uses/resolved.md). Native module-interface/fabric acceptance is tracked in what/is/the/native/axi/acceptance/status.md; broader package/procedural coverage is separate work.
