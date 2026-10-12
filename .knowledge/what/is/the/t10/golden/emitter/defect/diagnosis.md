---
status: green
revised_at: "2026-10-13T06:36:32+11:00"
---

The whole-design emitter `pigen_emit_systemverilog` (src/sv_emit.c) renders OPAQUE/MODULE/INSTANCE/EQUATION/PROCESS from the ordered output model and the named RTL records. Its committed draft (e97d477) failed the corrected contract on three verified defects, all now fixed:

1. Infinite padding loop (emit_object): `while (strlen(direction) < 6) buf_append_str(b, " ");` tested the CONSTANT `strlen(direction)`, so any `input`/`inout` port never terminated and OOMs. Fixed: pad a bounded number of spaces to six columns.
2. No nested-range skip (top-level walk): `for (i = 0; i < layout->item_count; i++)` over `item->kind` handled only OPAQUE and MODULE and hit `default: goto fail` on a module's nested children (the model is a flat array; t9-nesting pins `item_count` including nested children). Fixed: after a MODULE item, skip past its nested range `item->layout`; those render inside the module emit.
3. Rendering mismatches: (a) `logic` was emitted only when width != 1 but the contract requires it ALWAYS present (a width-1 port still emits `input  logic <name>,`); (b) the packed `[h:l]` range was dropped/misplaced for width > 1; (c) the port-header comma rule (last port takes no comma) and newline placement differed; (d) the concatenation join drifted to `{c, e}` (", ") to match the golden, contradicting the canonical form.

Verified CORRECT and unchanged in the fixed emitter: the re-scoped instance form `  instance #(8) (.clk(clk));`; the non-RESIZE conversion `signed'(16'(a))`; the equation and the guarded process. The expression renderer (pigen_sv_render_expression) is separate and green on main.

The canonical CONCATENATION join is `{a,b,...}` (","), as the contract leaf and the green-on-main t10-expr-render section (`{1,2,3}`) pin; draft e97d477 already used `","`. Two WHOLE-DESIGN WITNESS defects in the t10-golden golden keep it red even on the fixed emitter (neither is an emitter defect):
- CONCATENATION JOIN: the golden line `  assign d = {c, e};` (", ") contradicts the canonical `","`. One renderer cannot emit both, so the golden's `{c, e}` is re-scoped to `{c,e}`; the emitter uses `","`.
- TRAILING OPAQUE SPAN: the design source is 75 bytes and its trailing OPAQUE span is [48:74] = `// trailing golden comment` (no newline), but the golden ends `// trailing golden comment\n` and the leading span [0:26] includes its `\n`. An opaque span is copied verbatim and no newline is invented, so [48:74] can never yield the trailing `\n`; the span end is corrected to 75.

Resolution: with the three emitter defects fixed and the two witness defects corrected (join `{c,e}`, trailing span [48:75]), the golden renders byte-exact and `make sv-emit-test T=t10-golden` is green; the instance line stays re-scoped (path A, what/is/the/terminal/systemverilog/emitter/instance/primitive/ownership.md). The t10-skeleton empty-model `-1` pin is superseded: the whole-design emitter returns 0 for a 0-item model, so that pin retires in the change that lands the emitter.
