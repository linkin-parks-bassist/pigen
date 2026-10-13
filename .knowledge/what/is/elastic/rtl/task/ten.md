---
status: green
revised_at: "2026-10-12T09:08:26+11:00"
---

Task 10 is the terminal SystemVerilog emitter on the structured path. `pigen_emit_systemverilog(sources, layout, rtl, output_string)` consumes validated ordered coverage and named RTL, copies bounded opaque spans exactly, and renders from resolved RTL kind/operator/type records and final names. It never tokenizes/searches source or queries semantic owners. The signature, ownership and exact formatting belong to what/is/the/terminal/systemverilog/emitter/contract.md.

The public header exposes the whole-design 0/-1 entry and `pigen_sv_render_expression(rtl, expr, text)`, a thin wrapper over the file-local renderer. The precedence-aware expression renderer is implemented: INTEGER/BITS, OBJECT, UNARY/BINARY, CONDITIONAL, kind-aware CONVERSION, INDEX, SELECT and CONCATENATION, with fixed precedence, structurally necessary parentheses and one private operator table. Conversions use operand/target RTL types only. The non-RESIZE full cast `signed'(width'(operand))` preserves width/signedness; correcting the invalid leading-apostrophe golden to `signed'(16'(a))` does not make the accepted-red whole-design packet green.

Whole-design acceptance remains unresolved. The corrected golden requests instance/parameter spellings absent from its permitted input records and width-one port formatting inconsistent with the owning contract. Manager command `verify-t10-contract-coherence` owns full witness representability, formatting and prerequisite repair. Implementers must not hardcode fixture spellings or bypass record ownership.

Remaining Task 10 work is coherent whole-design golden emission, then no-partial-output and structural no-search gates. The golden covers exact leading/trailing opaque comments, signed conversion, concatenation, primitive parameters, an equation and a guarded update. Malformed coverage, unnamed objects, invalid children and out-of-bounds spans must leave no partial output. `sv_emit.c` must contain no `strstr`, `strchr`, `pigen_symbol_lookup`, `pigen_data_type_` or `pigen_transfer_type_` matches. Gate: `make sv-emit-test output-model-test rtl-test`. Files: `include/pigen/sv_emit.h`, `src/sv_emit.c`, `tests/sv_emit_test.c`, `Makefile`.

Expression enforcement is partial: the green expression-render section pins 9/26 binary operators, 4/10 unary operators (non-reduction), 1/7 conversion kinds (INTEGER_RESIZE), 2/3 select kinds (RANGE/INDEXED_UP), three precedence cases and NULL-argument edges. Remaining enforcement requires 17 binary operators, six reduction unaries, six conversion kinds and SELECT INDEXED_DOWN. Until their packet lands green, omitted/misspelled unpinned entries can pass the sample. `src/rtl.c` admits every enum by range check, so lowering can reach those entries.
