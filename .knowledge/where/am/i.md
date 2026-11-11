---
status: green
revised_at: "2026-11-11T16:15:18+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Pigen is an active personal C17 source-to-source compiler project extending SystemVerilog with ready/valid transfers, elastic pipelines, FSMs and routed fabrics. It emits readable synthesizable SV and storage primitives. Employer/partner/customer material is excluded.

This checkout is **pigen-fork**, a delivery fork of /home/david/Projects/pigen. The production `./pigen` is the structured frontend (src/pigen.c through src/compile.c): preprocess, data-first syntax, semantic types/expressions/scopes, RTL lowering, naming, output model and SV emission. The classic textual CLI is removed; prototype sources remain only as quarantined reference material.

Core transfers, pipelines, FSMs, validity actions, parameterized children, parent child-port endpoints and buffered fabric routing with arbitration/SVG are implemented. See what/is/broken.md for capability limits and what/is/the/plan.md for remaining work.

Mixed cases lower through native SystemVerilog combinational case decoders and scalar branch guards. Native case/casez/casex semantics preserve common width/signedness, wildcard matching and branch priority. Simulations cover ordinary selector evaluation, nested/ternary/concatenated labels, optional default colons, unbraced if/else, arithmetic widening, signedness, unknown selectors and wildcard comparisons against native reference cases. Full verification passes. Mixed selector scheduling and qualified cases remain open; decoders currently evaluate selectors combinationally.

Repository topology: src/ structured owners plus quarantined prototype sources; include/pigen/ C interfaces; rtl/pigen_primitives.sv storage; tests/ C/hardware regressions; examples/ designs including migrated biquad examples; .knowledge/ the sole local documentation and architectural answer authority. README.md is David's public introduction restored from d14aef2; preserve it.

- how/ answers mechanisms: how/does/an/atomic/transfer/fire.md, how/to/write/runnable/prototype/declarations.md, how/to/test/the/structured/foundation.md.
- what/ answers definitions/current work: what/is/the/spec.md, what/is/the/plan.md, what/is/broken.md.
- where/ locates code: where/are/compiler/owner/interfaces.md.
- why/ gives rationale: why/is/buffered/self/consumption/forbidden.md, why/use/three/port/fabric/routers.md.
- does/ answers behavior: does/the/structured/frontend/run/in/production.md.
- when/ gives gates: when/can/production/switch/to/the/structured/compiler.md.

The structured frontend implements provenance and preprocessing, type/declaration syntax, scopes/symbols, canonical types/shapes/integers, typed expressions/predicates/domains, one signal arena, transfers/incidence/ownership, storage controls, primitive instances, pipeline/FSM semantic expansion, acceptance/introspection intrinsics, structural instance/fabric syntax and metadata, retained ordinary-SV items, multi-module output and terminal emission. OPAQUE spans are byte-contiguous; retained-item spans may overlap by parser gaps. Icarus Verilog 13 and Verilator 5.020 are installed; lint stays warnings-fatal. The local tree is the sole project documentation authority; the immutable Kestrel reference remains applicable.
