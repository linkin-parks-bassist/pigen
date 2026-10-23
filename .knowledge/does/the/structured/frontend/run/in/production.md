---
status: green
revised_at: "2026-10-24T08:53:17+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

No. The production Makefile `pigen` target links the textual prototype (`src/pigen.c`, blocks, assignments, declarations, procedural, transfer, pipeline, FSM, lexer and util), excluding the structured parser, resolver, semantic model, RTL lowering and terminal emitter. Production retains rewritten text, rescans, generated-name lookup, marker comments and feature-local models. It still has lexical and procedural parsing; describing the entire repository as only a macro expander misses the separate structured implementation.

The public C API `pigen_compile_source` in `src/compile.c` separately calls preprocessing, `pigen_parse_syntax`, `pigen_resolve_semantics`, RTL declaration/transfer/module lowering, naming, ordered output building and SystemVerilog emission. This call sequence is not evidence of a working end-to-end compiler: the composer checks `resolve_ret != 0` as failure, whereas the resolver returns 1 on success. `tests/compile_test.c` explicitly asserts successful direct resolution returns 1 and then expects composition to return -1 with no output. Its skeleton tests expect failures; its simulation-infrastructure test only checks that iverilog and vvp exist. `make compile-test` passes these tests, not a successful structured compilation.

The Makefile `vertical-slice-test` invokes production `./pigen` on `examples/buf_pipeline.pigen` and simulates that output; `verify` includes this prototype test and does not depend on `compile-test`. Consequently a green verification run does not establish structured composition/simulation acceptance. Claims of complete Task 11 composition need reconciliation against this evidence.

Structured pipeline identity and stage-law owners exist separately, but `src/compile.c` and `src/resolve.c` do not call them; the compiler test link command excludes their implementation files. The resolver's module walk currently handles parameters, typedefs, signals and clocked processes, not a complete retained-feature migration.

Do not add a validator bridge, per-file compiler choice or fallback. Production switches only after the retained-feature migration gates are satisfied; see `what/are/the/remaining/architecture/migration/gates.md`.
