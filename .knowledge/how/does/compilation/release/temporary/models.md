---
status: green
revised_at: "2026-11-17T17:42:00+11:00"
---

pigen_compile_source in src/compile.c owns preprocessing, syntax, semantic, RTL-lowering and RTL-model storage for one compilation. Every cleanup path after RTL initialization releases both pigen_rtl_lowering_free and pigen_free_rtl_model. The lowering context borrows the RTL model. Output-model failure releases its partial model; normal emission releases it before the remaining arenas.

A pigen_compile_result owns its generated SystemVerilog string and pigen_fabric_diagrams array. Each diagram owns its name and SVG text. The result is zeroed completely on entry, including failures. On success, diagrams transfer from the RTL model to the result and the model's array is cleared; without a requested result, model cleanup frees the diagrams and compilation frees the emitted SV. pigen_free_compile_result releases all returned artifacts and is safe to repeat.

An internally allocated source manager releases source files before its manager allocation. A supplied manager remains caller-owned. Tree lowering frees temporary port descriptions, edge arrays and topology storage on success/failure; the RTL model owns published instances/equations and diagrams. src/blocks.c owns topology/SVG allocation helpers.

tests/fabric_compile_test.c exercises internal/borrowed source managers, returned/discarded outputs, artifact lifetime, repeated release and poisoned-result failure initialization. make fabric-compile-test runs it in test/verify and accepts sanitizer CFLAGS. tests/inline_fabric_tree_test.py also exercises the real CLI, including rejected interfaces and diagram-option failures preserving output.

The former native-routing-compile-test is archived with quarantined native AXI fabric integration; it is not active API coverage. Semantic temporary-table lifetimes remain owned by how/to/call/semantic/resolution.md.
