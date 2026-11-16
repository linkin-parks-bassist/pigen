---
status: green
revised_at: "2026-11-15T11:04:25+11:00"
---

pigen_compile_source in src/compile.c owns its preprocessing, syntax, semantic, RTL-lowering and RTL-model storage for one compilation. Every cleanup path after RTL initialization releases both pigen_rtl_lowering_free and pigen_free_rtl_model; the lowering context borrows the RTL model and does not destroy it. Output-model construction failure releases its partial output model. Normal emission releases the output model before the remaining compiler arenas.

The returned pigen_compile_result owns only its generated SystemVerilog string; callers release it with pigen_free_compile_result. If no result is requested, compilation frees the emitted string itself. An internally allocated source manager releases its source files before its manager allocation. A supplied source manager remains caller-owned and retains added source files until pigen_free_sources.

tests/native_routing_compile_test.c exercises native Stream switching/arbitration with borrowed and internal source managers, returned and discarded output, and a fabric-lowering failure after RTL allocation. make native-routing-compile-test runs that API witness and participates in test/verify. The same target accepts sanitizer CFLAGS to check the ownership paths. The public routing example also exercises compilation through the CLI under ASan/UBSan.

Semantic resolution's model and temporary-table lifetimes are separate (how/to/call/semantic/resolution.md). Output/error results remain subject to their public include/pigen/compile.h contract.
