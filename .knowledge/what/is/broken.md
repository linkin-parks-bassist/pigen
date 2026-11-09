---
status: green
revised_at: "2026-11-10T09:03:12+11:00"
---

The structured frontend is the production compiler and lowers datapath, pipeline, FSM, acceptance behavior and buffered inline fabric links, with standard SV compatibility for ordinary module items, but the language is not complete.

Working: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers with real primitives; clocked processes; inline pipelines (both stage forms); FSMs with state register/reset/guards/gotos; transfer-and-branch; `accepts`/`valid`/`ready`/`peek`; multiple modules; verbatim ordinary `assign` and standard child instantiations; semantic instance/port metadata; inline fabric endpoint wiring with direct `>` combinational links and routed `->`/`-->` links buffered through a pigen_skid stage. Simulations pass for the buf chain, arithmetic pipeline, FSM counter, README sender FSM, accepts()-based sender, standard child instance and a two-leaf fabric routed link.

Defects and gaps:
- **Fabric lacks routing networks.** No balanced three-port router topology, no round-robin arbitration, and no SVG/topology manifest. Multiple routed links to one destination are rejected at compile time rather than arbitrated.
- **Validity actions missing.** `validate`, `invalidate` and `flush` are not implemented.
- **semantic-test is red.** Its alias conversion assertion expects IDENTITY where the ordinary-SV arithmetic extension yields a common integer conversion.
- **Other owner tests are partially stale.** Nine rtl-lower sections and two sv-emit sections pin superseded contracts.
- **Expression coverage is partial.** Aliases and some conditional/conversion forms remain incomplete.
- **Examples are partially migrated.** Data-first and verified: buf_pipeline, compiler_pipeline, guarded_pipeline, output_pipeline, port_pipeline, skid_pipeline, fifo_pipeline. join_pipeline migration failed its timing check and was reverted; fixed_point_mac, text, biquad_bank and df1_biquad_bandpass remain legacy.

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
