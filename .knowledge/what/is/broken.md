---
status: green
revised_at: "2026-11-10T09:31:45+11:00"
---

The structured frontend is the production compiler and lowers datapath, pipeline, FSM, acceptance behavior, buffered inline fabric links and validity actions, with standard SV compatibility for ordinary module items, but the language is not complete.

Working: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers with real primitives; clocked processes; inline pipelines (both stage forms); FSMs; transfer-and-branch; `accepts`/`valid`/`ready`/`peek`; `validate`/`invalidate`/`flush` with defined zero defaults on undriven controls; multiple modules; verbatim ordinary `assign` and standard child instantiations; semantic instance/port metadata; inline fabric endpoint wiring with direct `>` links and skid-buffered routed links. `semantic-test`, `compile-test` and `sv-emit-test` are green. Simulations pass with bounded timeouts for the buf chain, compiler/guarded/output/port/skid/fifo/join pipelines, fixed-point MAC, FSM counter, README sender FSM, accepts()-based sender, standard child instance and a two-leaf fabric routed link.

Defects and gaps:
- **Fabric lacks routing networks.** No balanced three-port router topology, no round-robin arbitration, no SVG manifest; multiple routed links to one destination are rejected.
- **Nine rtl-lower sections remain stale**, pinning superseded endpoint/fire/instance contracts.
- **Expression/lvalue coverage is partial.** Concatenation lvalues/destinations are not lowered (blocking `text`/`df1_biquad_bandpass`), and aliases/some conditional forms remain incomplete.
- **Examples are partially migrated.** Data-first and verified: buf, compiler, guarded, output, port, skid, fifo, join, fixed_point_mac. `text`, `biquad_bank` and `df1_biquad_bandpass` remain legacy and need concatenation/aggregate support.

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
