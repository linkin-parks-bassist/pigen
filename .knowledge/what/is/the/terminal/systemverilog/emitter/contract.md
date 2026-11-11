---
status: green
revised_at: "2026-11-11T17:39:29+11:00"
---

pigen_emit_systemverilog is the structured compiler's terminal renderer. It consumes finished named RTL and output layout, never semantic symbols or transfer types.

Signature: `int pigen_emit_systemverilog(const pigen_source_manager *sources, const pigen_output_model *layout, const pigen_rtl_model *rtl, char **output_string);`. Sources own retained text; layout orders records; RTL owns resolved hardware/names. Success returns 0 and a newly allocated NUL-terminated string owned by the caller. Failure returns -1, leaves output NULL and publishes no partial text. Validation precedes rendering into a private buffer. Empty designs yield allocated empty strings; a NULL output argument is tolerated.

Layout renders top-level items in order. Modules render port headers, internal declarations, then remaining items and endmodule. OPAQUE/RETAINED spans preserve source text; object, instance, equation and process items render RTL records. Lines are newline-terminated.

Declarations use the resolved base keyword, signedness, packed dimensions, final name and unpacked shape. Two-state/four-state types use bit/logic; native integer keywords remain native. Ports support input/output/inout with six-column directions. All packed dimensions preserve recorded left/right bounds, including ascending, nonzero and multidimensional coordinates; bounds may be values or expressions. Only dimensionless vector types fall back to [width-1:0]. One-bit [0:0] emits as scalar. Primitive PAYLOAD_T uses the same renderer. Dimensions never come from semantic lookup or reconstructed source.

Equations emit assign statements, using destination_expression when present or the base destination otherwise. Processes render recorded clock/edge and guarded updates. Primitive instances use enum, payload type, parameters and connection records; see what/is/the/terminal/systemverilog/emitter/instance/primitive/ownership.md.

Names come from the RTL names arena using record identity/origin; generated text is never searched to recover meaning.

Expressions render recursively with precedence and parentheses: literals, objects, unary/binary operators, conditionals, conversions, indices, range/indexed selections and concatenations. Literal rendering inspects every word: high data bits or any X/Z mask select sized binary output preserving all states; otherwise decimal output suffices. BITS carries its resolved width and explicit signed marker when signed, preserving arithmetic-shift/comparison semantics. INTEGER retains ordinary decimal form for host-word magnitudes and the negative sign when recorded. Multiword binary rendering uses resolved width, with absent upper bits zero-filled.

Select punctuation is [:], [+:] or [-:]. INTEGER_RESIZE uses a signedness cast without width; other conversions use target width/signedness, e.g. signed'(16'(a)). Children come from RTL expression ranges.

The test seam pigen_sv_render_expression returns allocated text or NULL. Public declarations are in include/pigen/sv_emit.h. The structural boundary forbids source-text searches and semantic APIs in sv_emit.c; retained-span and final-name reads are permitted.

Verification: sv-emit-test, output-model-test and rtl-test cover records/failures. wide-literal-test simulates 193-bit four-state literals and signed 128-bit shifts. child-port-endpoint-test covers packed coordinates, aliases, handshakes and ownership. make verify includes these gates; they do not establish every signed/symbolic combination.

Implementation: src/sv_emit.c; records: include/pigen/rtl.h; tests: tests/sv_emit_test.c, wide_literals and child_port_projection_wire fixtures.
