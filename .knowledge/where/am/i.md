---
status: green
revised_at: "2026-11-10T08:38:06+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Pigen is an active personal C17 source-to-source compiler project extending SystemVerilog with ready/valid transfers, elastic pipelines, FSMs and routed fabrics. It emits readable synthesizable SV and storage primitives. Employer/partner/customer material is excluded.

This checkout is **pigen-fork**, a delivery fork of /home/david/Projects/pigen. The production `./pigen` is the structured frontend (src/pigen.c through src/compile.c): preprocess, data-first syntax, semantic types/expressions/scopes, RTL lowering, naming, output model and SV emission. The classic textual CLI is removed; prototype sources remain only as quarantined reference material.

Working end-to-end: data-first declarations; abstract inputs with valid/ready ports; static/buffered transfers with real `pigen_*` primitives; clocked processes; inline pipelines (both stage forms); FSMs with state register/reset/guards/gotos; transfer-and-branch; `accepts`/`valid`/`ready`/`peek`; multi-module designs; verbatim ordinary SV items and standard child instantiations; semantic instance/port metadata; inline fabric endpoint wiring over explicit parent payload/valid/ready nets. Simulations pass for a buf chain, arithmetic pipeline, FSM counter, README sender FSM, accepts()-based sender, standard child instance and a two-leaf fabric routed link.

Fabric is currently wiring-only: routed links are combinational equations, with no routers, buffered ready breaks, arbitration or SVG. Also missing: `validate`/`invalidate`/`flush`; some alias/conditional expressions; examples migration; rebaseline of semantic-test's alias assertion and the nine rtl-lower/two sv-emit stale sections.

Repository topology: src/ structured owners plus quarantined prototype sources; include/pigen/ C interfaces; rtl/pigen_primitives.sv storage; tests/ C/hardware regressions; examples/ legacy prototype-dialect designs; .knowledge/ the sole local documentation and architectural answer authority. README.md is David's public introduction restored from d14aef2; preserve it.

- how/ answers mechanisms: how/does/an/atomic/transfer/fire.md, how/to/write/runnable/prototype/declarations.md, how/to/test/the/structured/foundation.md.
- what/ answers definitions/current work: what/is/the/spec.md, what/is/the/plan.md, what/is/broken.md.
- where/ locates code: where/are/compiler/owner/interfaces.md.
- why/ gives rationale: why/is/buffered/self/consumption/forbidden.md, why/use/three/port/fabric/routers.md.
- does/ answers behavior: does/the/structured/frontend/run/in/production.md.
- when/ gives gates: when/can/production/switch/to/the/structured/compiler.md.

The structured frontend implements provenance and preprocessing, type/declaration syntax, scopes/symbols, canonical types/shapes/integers, typed expressions/predicates/domains, one signal arena, transfers/incidence/ownership, storage controls, primitive instances, pipeline/FSM semantic expansion, acceptance/introspection intrinsics, structural instance/fabric syntax and metadata, retained ordinary-SV items, multi-module output and terminal emission. OPAQUE spans are byte-contiguous; retained-item spans may overlap by parser gaps. Icarus Verilog 13 and Verilator 5.020 are installed; lint stays warnings-fatal. The local tree is the sole project documentation authority; the immutable Kestrel reference remains applicable.
