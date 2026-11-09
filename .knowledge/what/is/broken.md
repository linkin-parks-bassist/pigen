---
status: green
revised_at: "2026-11-10T10:17:48+11:00"
---

The structured frontend is the production compiler and lowers datapath, pipeline, FSM, acceptance behavior, buffered inline fabric links, validity actions, grouped storage producers and aligned concatenation destinations, with standard SV compatibility for ordinary module items, but the language is not complete.

Working: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers with real primitives; clocked processes; inline pipelines (both stage forms); FSMs; transfer-and-branch; `accepts`/`valid`/`ready`/`peek`; `validate`/`invalidate`/`flush`; multiple mutually exclusive producers to one storage signal (payload mux + OR-of-fires valid); multiple modules; verbatim ordinary `assign` and standard child instantiations; semantic instance/port metadata; inline fabric endpoint wiring with direct links and skid-buffered routed links; concatenation destinations with per-child conversion. Updates carry RHS in value and fire in enable. Every structured owner test is green, rtl-lower 24/24.

Defects and gaps:
- **Fabric lacks routing networks.** No balanced three-port router topology, no round-robin arbitration, no SVG manifest; multiple routed links to one destination are rejected.
- **Expression/lvalue coverage is partial.** Typed parameters and mixed pigen-integer × logic arithmetic do not lower; concatenation needs an aligned RHS (no `_`/misaligned slicing); functions/structs remain incomplete.
- **Examples are mostly migrated.** Data-first and verified: buf, compiler, guarded, output, port, skid, fifo, join, fixed_point_mac, text. `biquad_bank` and `df1_biquad_bandpass` remain legacy (structs/macros/functions).

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
