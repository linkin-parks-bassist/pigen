---
status: green
revised_at: "2026-10-12T23:20:39+11:00"
---

The committed whole-design emitter draft (commit e97d477 on work/t10-golden-impl, src/sv_emit.c, 1076 lines) renders OPAQUE/MODULE/INSTANCE/EQUATION/PROCESS but fails the corrected contract on three verified defects; the re-scoped t10-golden golden (`  instance #(8) (.clk(clk));`) therefore cannot go green on it:

1. Hard crash (infinite padding loop, emit_object): after `buf_append_str(b, direction)` it loops `while (strlen(direction) < 6) buf_append_str(b, " ");` — it appends to the buffer but tests the CONSTANT `strlen(direction)`, so any `input`/`inout` port (length 5) never terminates and OOMs (verified: buffer to 134217729 bytes then SIGKILL). Fix: copy the direction length once, append the direction, then pad a fixed number of spaces to reach six columns.

2. Logic failure (top-level walk): the walk `for (i = 0; i < layout->item_count; i++)` over `item->kind` handles only OPAQUE and MODULE and hits `default: goto fail` otherwise. But the ordered output model is a flat array whose `item_count` includes each module's nested children (t9-nesting pins this); the first nested child (a structured item) hits `default` and returns -1. The walk must skip items inside a module's nested range (those render inside via the module emit).

3. Rendering mismatches (vs corrected contract): (a) the `logic` keyword is emitted only when width != 1, but the corrected contract requires it ALWAYS present (a width-1 port still emits `input  logic <name>,`); (b) the packed `[h:l]` range is dropped / misplaced for width > 1; (c) the port-header comma rule and newline placement differ (last port takes no comma); (d) the concatenation join is `{c,e}` not `{c, e}`.

Verified CORRECT in e97d477 (do not change): the re-scoped instance form `  instance #(8) (.clk(clk));` (emit_instance renders the synthetic INSTANCE role name `instance`, `#(8)` from the integer parameter value, `(.clk(clk))` from the single connection); the non-RESIZE conversion `signed'(16'(a))`; the equation (`assign d = ...;`) and process (`always @(posedge clk)` + guarded `<=` updates). The expression renderer (pigen_sv_render_expression) is separate and already green on main.

Resolution: fix the three defects in src/sv_emit.c (the emitter's owner) so the whole-design golden renders byte-exact per what/is/the/terminal/systemverilog/emitter/contract.md; the instance line stays re-scoped (path A, see what/is/the/terminal/systemverilog/emitter/instance/primitive/ownership.md). The t10-skeleton empty-model -1 pin is superseded (the whole-design emitter returns 0 for a 0-item model) and retires in the change that lands it.
