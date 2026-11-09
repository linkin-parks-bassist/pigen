---
status: green
revised_at: "2026-11-10T09:39:24+11:00"
---

The structured frontend is the production compiler and lowers datapath, pipeline, FSM, acceptance behavior, buffered inline fabric links, validity actions and aligned concatenation destinations, with standard SV compatibility for ordinary module items, but the language is not complete.

Working: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers with real primitives; clocked processes; inline pipelines (both stage forms); FSMs; transfer-and-branch; `accepts`/`valid`/`ready`/`peek`; `validate`/`invalidate`/`flush` with zero defaults on undriven controls; multiple modules; verbatim ordinary `assign` and standard child instantiations; semantic instance/port metadata; inline fabric endpoint wiring with direct `>` links and skid-buffered routed links; concatenation destinations when the RHS is a same-arity concatenation (each child converts to its destination type). `semantic-test`, `compile-test`, `sv-emit-test` and `concat-dest-test` are green. Simulations pass with bounded timeouts for the buf chain, compiler/guarded/output/port/skid/fifo/join pipelines, fixed-point MAC, FSM counter, README sender FSM, accepts()-based sender, standard child instance and a two-leaf fabric routed link.

Defects and gaps:
- **Fabric lacks routing networks.** No balanced three-port router topology, no round-robin arbitration, no SVG manifest; multiple routed links to one destination are rejected.
- **Nine rtl-lower sections remain stale**, pinning superseded endpoint/fire/instance contracts.
- **Expression/lvalue coverage is partial.** Concatenation destinations require an aligned RHS concatenation (no misaligned slicing or `_` placeholders); functions, structs/aggregates and some conditional forms remain incomplete.
- **Examples are partially migrated.** Data-first and verified: buf, compiler, guarded, output, port, skid, fifo, join, fixed_point_mac. `text`, `biquad_bank` and `df1_biquad_bandpass` remain legacy and need placeholder/aggregate/function support.

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
