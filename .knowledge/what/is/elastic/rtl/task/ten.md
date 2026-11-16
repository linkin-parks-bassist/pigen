---
status: green
revised_at: "2026-11-15T14:12:21+11:00"
---

Task 10 is the implemented terminal SystemVerilog renderer in include/pigen/sv_emit.h and src/sv_emit.c. It consumes finished RTL identities, types, names and ordered output layout; it does not reconstruct semantics from generated text. The complete API, ownership and rendering rules are in what/is/the/terminal/systemverilog/emitter/contract.md.

pigen_emit_systemverilog returns 0 with an allocated NUL-terminated string, including an allocated empty string for an empty design. Failure returns -1 and leaves output NULL. Validation precedes private-buffer rendering. Opaque spans retain exact bytes and coverage; malformed coverage, missing names, invalid expression/statement references and invalid source spans fail without partial output. The expression-rendering seam is pigen_sv_render_expression.

RTL records support native primitives with payload/parameters, owned generic module definitions and directional connections, plus the legacy positional fallback used by the golden fixture. The fallback's synthetic `instance #(8)` does not limit production instance naming (what/is/the/terminal/systemverilog/emitter/instance/primitive/ownership.md). Declarations use bit/logic according to resolved state domain; width-one declarations retain their type. Conversions carry explicit width and signedness. Concatenation punctuation is canonical comma-separated text.

Clocked processes render recorded updates. Typed combinational processes render ordered blocks, if/else and blocking assignments, preserving branch entry and read-after-write behavior (how/are/scoped/combinational/processes/represented.md).

Evidence: tests/sv_emit_test.c contains t10-skeleton, t10-expr-render, t10-no-partial-output, t10-combinational and t10-golden. The combinational section checks valid emission and invalid root/value/child rejection; the golden checks exact whole-design layout. make sv-emit-test, output-model-test and rtl-test exercise these boundaries. The structural no-search gate excludes source-text searches and semantic/type/transfer lookup APIs in the backend. Covered witnesses and the broader simulation suite do not establish exhaustive operator or language-form coverage. Remaining language work belongs in what/is/the/plan.md.
