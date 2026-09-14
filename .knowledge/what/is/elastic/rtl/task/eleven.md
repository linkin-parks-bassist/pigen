---
status: "unverified"
created_at: "2026-09-13T15:05:52+10:00"
scope: "local"
source: "2026-09-01 approved elastic RTL design/plan full section audit; current code baseline; David documentation retirement 2026-09-14"
ingested_by: "Codex /root"
checked_at: "2026-09-13T15:07:12+10:00"
review_when: "Review when cited source contracts, implementation status or test evidence change."
updated_at: "2026-09-14T20:36:21+10:00"
---

Quarantined compiler composition. Create compile.h/compile.c/compile_test.c, vertical_slice.pigen and vertical_slice_tb.sv. Sequence all phases and reverse cleanup, preserve native span/origin errors. Integration covers data-first abstract inputs, buf/port/fifo[4]/skid and guarded arithmetic/projection/concat, opaque comments exact, no markers. Simulate /tmp emitted SV with Icarus for stable stalled payload/simultaneous push-pop/guard. Gate error phases and no compile/lowering/emitter linkage in prototype files; focused vertical-slice/backend tests. This is approved future work, not implemented status.

pigen_compile_source(sources, source, options, result, error) uses options.maximum_generated_bits (test example 4096), returns systemverilog in result, and pigen_free_compile_result destroys it. In-memory phase-error tests cover syntax, type mismatch, cross-domain transfer, ready cycle and unsupported Pigen-dependent opaque forms, with exact source/span and no output. Icarus compiles rtl/pigen_primitives.sv, /tmp/pigen-vertical-slice.sv and tests/vertical_slice_tb.sv with -g2012, then vvp runs the result. Holds ready low, checks stable payload, releases stalls, simultaneous FIFO push/pop and guarded transfer. Native origins/spans survive error adaptation; reverse cleanup on failure. Quarantine search for composer/lowering/emitter names in src/pigen.c, blocks.c, pipeline.c, fsm.c must have no matches. Files compile.h/c, compile_test.c, vertical_slice.pigen/tb.sv, Makefile.
