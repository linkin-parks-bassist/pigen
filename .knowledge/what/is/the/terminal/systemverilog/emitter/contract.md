---
status: green
revised_at: "2026-11-11T17:08:36+11:00"
---

pigen_emit_systemverilog is the structured compiler's terminal renderer. It consumes finished named RTL and the output layout, never semantic symbols or transfer types.

Signature: `int pigen_emit_systemverilog(const pigen_source_manager *sources, const pigen_output_model *layout, const pigen_rtl_model *rtl, char **output_string);`. Sources own retained text; layout orders output records; RTL owns resolved hardware and names. Success returns 0 and a newly allocated NUL-terminated string owned by the caller. Failure returns -1, leaves output NULL and publishes no partial text. Validation precedes rendering into a private buffer. Empty designs yield an allocated empty string; a NULL output argument is tolerated without publication.

Layout renders top-level items in order. Modules render port headers, internal declarations, then their remaining items, ending with endmodule. OPAQUE and RETAINED spans preserve source text; RTL object, instance, equation and process items render their records. Lines are newline-terminated.

Object declarations use the resolved base keyword, signedness, packed dimensions, final name and unpacked shape. Two-state/four-state types use bit/logic; native integer keywords remain native. Ports support input/output/inout with six-column directions. All packed dimensions preserve recorded left/right bounds, including ascending, nonzero and multidimensional coordinates; bounds may be values or RTL expressions. Only dimensionless vector types fall back to [width-1:0]. A one-bit [0:0] is emitted as a scalar. Primitive PAYLOAD_T parameters use the same packed-shape renderer. No semantic lookup or source reconstruction determines dimensions.

Equations emit assign statements, using destination_expression when present or the base destination object otherwise. Processes render their recorded clock/edge and guarded updates. Primitive instances use the primitive enum, payload type, parameters and connection records; the detailed owner is what/is/the/terminal/systemverilog/emitter/instance/primitive/ownership.md.

Names come from the RTL names arena using record identity/origin; generated text is never searched to recover meaning.

Expressions render recursively with operator precedence and necessary parentheses: integer/bit literals, objects, unary/binary operators, conditionals, conversions, indices, range/indexed-up/indexed-down selects and concatenations. Select punctuation is respectively [:], [+:] and [-:]. INTEGER_RESIZE uses a signedness cast without a width prefix; other conversions use the target width and signedness, e.g. signed'(16'(a)). Children come from RTL expression ranges.

The test seam pigen_sv_render_expression returns allocated expression text on success and NULL on failure. Public declarations are in include/pigen/sv_emit.h. The structural boundary forbids source-text searches and semantic owner APIs in sv_emit.c; retained-span reads and final-name reads are permitted.

Verification: make sv-emit-test, output-model-test and rtl-test cover record rendering and failure behavior; make verify includes repository gates. child-port-endpoint-test simulates equivalent range/indexed aliases for ascending/descending declarations, multidimensional packed storage, declaration order, handshakes and ownership errors. These witnesses do not establish every possible signed or symbolic bound combination.

Implementation: src/sv_emit.c; record definitions: include/pigen/rtl.h; tests: tests/sv_emit_test.c and child_port_projection_wire fixtures.
