---
status: green
revised_at: "2026-11-11T15:36:27+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Pigen is an active personal C17 source-to-source compiler project extending SystemVerilog with ready/valid transfers, elastic pipelines, FSMs and routed fabrics. It emits readable synthesizable SV and storage primitives. Employer/partner/customer material is excluded.

This checkout is **pigen-fork**, a delivery fork of /home/david/Projects/pigen. The production `./pigen` is the structured frontend (src/pigen.c through src/compile.c): preprocess, data-first syntax, semantic types/expressions/scopes, RTL lowering, naming, output model and SV emission. The classic textual CLI is removed; prototype sources remain only as quarantined reference material.

Core transfers, pipelines, FSMs, validity actions, parameterized children and buffered fabric routing with arbitration/SVG are implemented. See what/is/broken.md for capability limits and what/is/the/plan.md for remaining work.

Clocked case rewriting is limited to bodies identified as containing Pigen actions. Ordinary cases retain their original selector evaluation and control flow. Mixed cases use case equality (`===`), parenthesized operands, concatenated/comma-separated labels and complete if/else branch statements. Simulation witnesses cover single evaluation of an ordinary side-effecting selector, matching/default/unknown mixed selectors, concatenated labels and unbraced if/else bodies. Full `make -j8 --output-sync=target verify` passes. Remaining case gaps include mixed selector evaluation, common case width/signedness, nested case variants and bounded specialization convergence.

Repository topology: src/ structured owners plus quarantined prototype sources; include/pigen/ C interfaces; rtl/pigen_primitives.sv storage; tests/ C/hardware regressions; examples/ designs including migrated biquad examples; .knowledge/ the sole local documentation and architectural answer authority. README.md is David's public introduction restored from d14aef2; preserve it.

- how/ answers mechanisms: how/does/an/atomic/transfer/fire.md, how/to/write/runnable/prototype/declarations.md, how/to/test/the/structured/foundation.md.
- what/ answers definitions/current work: what/is/the/spec.md, what/is/the/plan.md, what/is/broken.md.
- where/ locates code: where/are/compiler/owner/interfaces.md.
- why/ gives rationale: why/is/buffered/self/consumption/forbidden.md, why/use/three/port/fabric/routers.md.
- does/ answers behavior: does/the/structured/frontend/run/in/production.md.
- when/ gives gates: when/can/production/switch/to/the/structured/compiler.md.

The structured frontend implements provenance and preprocessing, type/declaration syntax, scopes/symbols, canonical types/shapes/integers, typed expressions/predicates/domains, one signal arena, transfers/incidence/ownership, storage controls, primitive instances, pipeline/FSM semantic expansion, acceptance/introspection intrinsics, structural instance/fabric syntax and metadata, retained ordinary-SV items, multi-module output and terminal emission. OPAQUE spans are byte-contiguous; retained-item spans may overlap by parser gaps. Icarus Verilog 13 and Verilator 5.020 are installed; lint stays warnings-fatal. The local tree is the sole project documentation authority; the immutable Kestrel reference remains applicable.
