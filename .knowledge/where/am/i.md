---
status: green
revised_at: "2026-11-09T22:34:03+11:00"
checked_at: "2026-09-13T15:07:12+10:00"
---

Pigen is an active personal C17 source-to-source compiler project extending SystemVerilog with ready/valid transfers, elastic pipelines, FSMs and routed fabrics. It emits readable synthesizable SV and storage primitives. Employer/partner/customer material is excluded.

This checkout is **pigen-fork**, a delivery fork of /home/david/Projects/pigen. The production `./pigen` is the structured frontend (src/pigen.c through src/compile.c), built on the owner pipeline: source/preprocess, data-first syntax, semantic types/expressions/scopes, RTL lowering, naming, output model and SV emission. The classic textual CLI is removed; prototype sources remain only as quarantined reference material.

Working end-to-end on the structured path: data-first declarations; abstract inputs with generated valid/ready ports; static logic/reg/wire transfers with guards, negated predicates and assignment conversions; buf/port/fifo/skid storage as real `pigen_*` primitives with valid/ready/packet-in wiring and offer/fire equations; clocked processes; inline pipelines (fields and stage assignments resolve to ordinary buf/static signals and transfers, and a yield writes the pipeline output); FSMs (state register with reset, state-active guards, gotos and action transfers). Verified with behavioural simulations: a three-stage buf chain (12 ordered packets), a three-stage arithmetic pipeline with backpressure, and an FSM counter that starts, counts and returns to idle.

Not yet built: child-module instance syntax/resolution and inline fabric. Sized SV literals, aliases and some conditional forms remain partial. The `if (destination <= source)` transfer-as-condition sugar and the `accepts`/`peek`/validity intrinsics are not implemented. Nine rtl-lower sections and two sv-emit sections still pin the superseded storage/fire/instance contract and need rebaselining; all other structured C tests pass.

Repository topology: src/ structured owners plus quarantined prototype sources; include/pigen/ C interfaces; rtl/pigen_primitives.sv storage; tests/ C/hardware regressions; examples/ legacy prototype-dialect designs; .knowledge/ the sole local documentation and architectural answer authority. README.md is David's public introduction restored from d14aef2; preserve it. Other docs, notes, root Markdown and AGENTS.md are absent.

- how/ answers mechanisms: how/does/an/atomic/transfer/fire.md, how/to/write/runnable/prototype/declarations.md, how/to/test/the/structured/foundation.md.
- what/ answers definitions/current work: what/is/the/spec.md, what/is/the/plan.md, what/is/broken.md.
- where/ locates code: where/are/compiler/owner/interfaces.md.
- why/ gives rationale: why/is/buffered/self/consumption/forbidden.md, why/use/three/port/fabric/routers.md.
- does/ answers behavior: does/the/structured/frontend/run/in/production.md.
- when/ gives gates: when/can/production/switch/to/the/structured/compiler.md.

The structured frontend implements provenance and preprocessing, type/declaration syntax, scopes/symbols, canonical types/shapes/integers, typed expressions/predicates/domains, one signal arena, transfers/incidence/ownership, storage controls, primitive instances, pipeline/FSM semantic expansion, output process/instance records and terminal emission. OPAQUE spans are byte-contiguous. Icarus Verilog 13 and Verilator 5.020 are installed; Verilator lint stays warnings-fatal. The local tree is the sole project documentation authority; the immutable Kestrel reference remains applicable.
