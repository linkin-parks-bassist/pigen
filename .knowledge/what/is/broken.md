---
status: green
revised_at: "2026-11-10T10:10:02+11:00"
---

The structured frontend is the production compiler and lowers datapath, pipeline, FSM, acceptance behavior, buffered inline fabric links, validity actions and aligned concatenation destinations, with standard SV compatibility for ordinary module items, but the language is not complete.

Working: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers with real primitives; clocked processes; inline pipelines (both stage forms); FSMs; transfer-and-branch; `accepts`/`valid`/`ready`/`peek`; `validate`/`invalidate`/`flush` with zero defaults on undriven controls; multiple modules; verbatim ordinary `assign` and standard child instantiations; semantic instance/port metadata; inline fabric endpoint wiring with direct `>` links and skid-buffered routed links; concatenation destinations with aligned RHS children and per-child conversion. Updates carry the RHS in value and the fire identity in enable; guard atoms materialize explicit `== 1` / logical-not wrappers. Every owner test is green: semantic, compile, sv-emit, concat-dest and **rtl-lower (24/24)**.

Defects and gaps:
- **Fabric lacks routing networks.** No balanced three-port router topology, no round-robin arbitration, no SVG manifest; multiple routed links to one destination are rejected.
- **Expression/lvalue coverage is partial.** Typed parameters and mixed pigen-integer × logic arithmetic do not lower; concatenation needs an aligned RHS (no `_`/misaligned slicing); functions/structs remain incomplete.
- **Examples are partially migrated.** Data-first and verified: buf, compiler, guarded, output, port, skid, fifo, join, fixed_point_mac. `text`, `biquad_bank` and `df1_biquad_bandpass` remain legacy.

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
