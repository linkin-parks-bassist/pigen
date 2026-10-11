---
status: green
revised_at: "2026-10-12T00:41:35+11:00"
---

`pigen_emit_systemverilog` renders one finished, named RTL lowering to a single SystemVerilog text. It is the terminal emitter of the structured path (what/is/elastic/rtl/task/order.md) and consumes only already-resolved records; it never re-derives meaning.

Signature and ownership: `int pigen_emit_systemverilog(const pigen_source_manager *sources, const pigen_output_model *layout, const pigen_rtl_model *rtl, char **output_string);`. `sources` owns the opaque span text; `layout` is the ordered output model built by `pigen_build_output_model`; `rtl` is the finished RTL model whose `names` arena is already populated by `pigen_rtl_assign_names`; `output_string` is an out-param. Returns 0 on success, with `*output_string` a newly-allocated, NUL-terminated string the caller frees, or -1 on any failure leaving `*output_string` NULL and no partial output (how/should/compiler/builders/fail.md): it validates first, renders into a private buffer, and commits `*output_string` only on success.

Emission rule: walk `layout` items in array order (a MODULE item's nested range is the owner-managed layout of its children, rendered inside the module). Per item kind:
- OPAQUE: copy its span's text verbatim from `sources` via `pigen_source_span_text` (no re-parse, no trim).
- MODULE: emit the module header and close `endmodule`; its nested items render inside, in their own order.
- RTL_OBJECT / RTL_INSTANCE / RTL_EQUATION / RTL_PROCESS: render from the matching RTL record and its final name.

Names: a record's final name is the `pigen_rtl_name` in `rtl->names` whose identity (role kind + origin span, per `same_identity`) matches the record's role and origin; render `name->text` directly. No generated text is ever re-searched or re-interpreted.

Expression rendering: recursive descent over `pigen_rtl_expr` kind — one fixed precedence per kind, structurally necessary parentheses, one private operator table. Kinds: INTEGER/BITS (literals), OBJECT (a name), UNARY/BINARY (operator from `as.unary.resolution`/`as.binary.resolution`), CONDITIONAL (`? :`), CONVERSION (rendered from the RTL operand and target type only), INDEX (`[idx]`), SELECT (`[left:right]` / `[idx]` per `as.select.kind`), CONCATENATION (`{a,b,...}` over children). Child order is `first_child`/`child_count` via `pigen_rtl_expr_children`.

The public expression test seam `int pigen_sv_render_expression(const pigen_rtl_model *rtl, const pigen_rtl_expr *expr, char **text);` in `include/pigen/sv_emit.h` renders one finished, named expression. It returns 0 with `*text` a newly-allocated NUL-terminated string the caller frees, or -1 leaving `*text` NULL and no partial output; the model's `names` arena must already be populated, and an OBJECT expression renders the final name whose (role, origin) identity matches its object. It is the seam the Task 10 expression-renderer test packet drives; the precedence rendering it delegates to is a file-local stub (-1) until the separate renderer item.

Types: widths, signedness and packed dimensions come from `pigen_rtl_type` (`signedness`, `state_domain`, `width`, `dimensions`, `dimension_count`) only. Conversions render from the RTL operand expression and target type; the emitter never consults a semantic symbol, data type, or transfer type.

Structural boundary (enforced, not a comment): `sv_emit.c` contains no source-text search or semantic-owner lookup. A structural grep for `strstr`, `strchr`, `pigen_symbol_lookup`, `pigen_data_type_`, and `pigen_transfer_type_` must return no matches; the only text reads are `pigen_source_span_text` (opaque spans) and `pigen_rtl_name_get` (final names).

Build seam: `make sv-emit-test` compiles `tests/sv_emit_test.c` with `src/sv_emit.c` plus the minimal record owners (`src/rtl.c`, `src/source.c`, `src/rtl_name.c`, `src/util.c`) and runs `/tmp/pigen-sv-emit-test $(T)`; each section is its own `tests/contracts.json` entry (`make sv-emit-test T=t10-<section>`). The gate is `make sv-emit-test`, `make output-model-test` and `make rtl-test` green.

Files: `include/pigen/sv_emit.h`, `src/sv_emit.c`, `tests/sv_emit_test.c`, `Makefile`.
