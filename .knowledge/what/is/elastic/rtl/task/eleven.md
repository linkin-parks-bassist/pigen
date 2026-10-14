---
status: green
revised_at: "2026-10-14T21:45:36+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Quarantined compiler composition and simulation remain incomplete. The accepted public interface in include/pigen/compile.h takes sources, source_path, source_text, source_length, options, result and error; pigen_compile_source returns 0 on success and -1 on failure. The result owns allocated SystemVerilog text/length until pigen_free_compile_result clears it. This replaces the earlier abbreviated planned signature. source_path labels the explicitly supplied in-memory source for diagnostics; its filesystem existence does not establish rejection. options.maximum_generated_bits must be respected. The composer sequencing owner defines phases, diagnostics and reverse cleanup.

Declarations, an adapter body and registered compile tests exist on accepted main. Successful composition is not established. Main has failure-only tests; a reviewed hidden packet independently validates ordinary SystemVerilog syntax/semantics and requires public output, length and cleanup. Ordinary passthrough does not establish Pigen-owned translation or the full phase sequence. Add an independently valid owned-declaration witness before accepting composition. Diagnostic tests still accept generic messages without native origin/span assertions. The remaining diagnostic, cleanup and simulation requirements still apply.

Integration must cover data-first abstract inputs, buf/port/fifo[4]/skid, guarded arithmetic, projection and concat, with exact opaque comments and no markers. In-memory syntax, type, cross-domain, ready-cycle and unsupported Pigen-dependent opaque failures must preserve exact native source/span/origin and produce no output. Compilation options include the 4096-bit example limit.

Provide vertical_slice.pigen and vertical_slice_tb.sv. Icarus compiles rtl/pigen_primitives.sv, emitted vertical-slice SystemVerilog and the testbench with -g2012, then vvp verifies stable payload under stalls, released stalls, simultaneous FIFO push/pop and guarded transfer. Invocation-owned build artifacts must avoid shared fixed temporary executables. Keep prototype src/pigen.c, blocks.c, pipeline.c and fsm.c free of structured composer/lowering/emitter links. Focused backend and vertical-slice checks establish this quarantine. Expected work areas are compile.h/c, compile_test.c, vertical_slice.pigen/tb.sv and build registration.
