---
status: green
revised_at: "2026-11-16T11:23:58+11:00"
---

Static signal declarator dimensions remain unpacked RTL object shape, separate from packed data type. src/rtl_lower.c signal_shape_suffix evaluates explicit range bounds as signed integers when possible, preserving negative, ascending and descending coordinates. Nonnegative wide bounds retain the unsigned evaluation path. Count dimensions emit descending [N-1:0] ranges. add_named_object attaches the resulting shape to the payload object; src/sv_emit.c appends that finished shape after the final object name in ports and internal declarations.

A non-scalar static declaration whose shape cannot be lowered rejects during declaration lowering. It must not silently become a scalar when coordinate evaluation fails. Ordinary packed dimensions remain owned by RTL types; static packed records can carry a prepared declaration_type while retaining this separate unpacked suffix (how/are/static/packed/record/declarations/emitted.md). Semantic indexing consumes unpacked dimensions before packed data (how/to/index/shaped/signals.md). Shape is not recovered from emitted brackets. Typed data-array initializers retain this object shape and emit native nested assignment patterns (how/are/unpacked/data/assignment/patterns/resolved.md).

Evidence: the generated constant/elastic observation cases in tests/native_array_test.py use an output uint[8] wire tags[-2:-1][2] array and independently check all four coordinates through child outputs. Icarus whole-array connections require compatible element state domains; the external bench uses wire bit [7:0] for the compiler's two-state numerical elements. Native AXI endpoint collections have distinct per-lane flattened pin rules (how/are/native/axi/endpoint/arrays/represented.md).
