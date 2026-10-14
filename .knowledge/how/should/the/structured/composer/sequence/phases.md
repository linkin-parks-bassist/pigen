---
status: green
revised_at: "2026-10-14T21:45:36+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

The structured composer must invoke preprocessing, syntax parsing, semantic resolution, RTL module lowering, RTL naming, output-model construction and SystemVerilog emission in that order. Preserve native origin/span diagnostics and free completed models in reverse order. Do not use prototype lexer/rewrite APIs.

The public pigen_compile_source adapter now exists in include/pigen/compile.h and src/compile.c. Its presence does not establish this sequence: the current body calls pigen_lex_source, initializes semantic/lowering objects without completing the required semantic-resolution/lowering/naming/output-building path, and ignores compilation options. Main's compile_test.c still expects failure. A reviewed hidden packet validates ordinary source and requires output, but passthrough can satisfy it without executing semantic resolution, lowering or naming. Composition acceptance also needs an independently valid Pigen-owned declaration translated into legal SystemVerilog, then native diagnostics, options and cleanup checks. Evidence: accepted main's compile.h, compile.c and tests/compile_test.c.
