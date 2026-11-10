---
status: green
revised_at: "2026-11-10T16:06:30+11:00"
---

The structured frontend is the production compiler and lowers datapath, pipeline, FSM, acceptance behavior, buffered inline and standalone fabric routing, validity actions, grouped storage producers and concatenation destinations, with broad ordinary-SV preservation. The language is still incomplete.

Working and verified: data-first declarations; abstract inputs with valid/ready ports; static and buffered transfers with real primitives; clocked processes; inline pipelines (both stage forms); FSMs; transfer-and-branch; `accepts`/`valid`/`ready`/`peek`; `validate`/`invalidate`/`flush`; grouped storage producers; multiple modules; ordinary `assign`, child instances, `generate`/`genvar`, `initial`/`final`, `always_comb`/`always_latch`, typedefs and parameters; semantic instance/port metadata; inline direct links and skid-buffered routed links; standalone fabric networks with balanced three-port routers, round-robin arbitration, registered occupancy, path metadata and deterministic SVG; concatenation destinations with `_` placeholders, nested and misaligned slices; typed and integer parameters, `$clog2`, and mixed integer/logic arithmetic; ordinary interface-typed and directionless-port modules emitted verbatim. Every owner suite is green, including rtl-lower 24/24, and both biquad examples are migrated and green.

Defects and gaps:
- **Parameterized Pigen children are unsupported.** Modules containing Pigen structured constructs are parsed structurally and emit no header parameter list, so an overridden child instance would keep default port widths. Ordinary SystemVerilog parameterized children and instances already elaborate verbatim.
- **Mixed structured/retained clocked bodies diagnose.** A clocked body cannot combine Pigen structured constructs with retained function/task calls or other unsupported syntax; it stops with a source-located diagnostic rather than emitting raw Pigen.
- **Ordinary-SV preservation is audit-driven.** Unrecognized ordinary module items are retained, but advanced constructs beyond the currently witnessed assignments, declarations, generate/initial/comb, parameters, typedefs, child instances and interface/directionless ports still need auditing; unsupported forms must be recovered rather than silently dropped.
- **Deferred to Pigen 1.0:** semantic ready-cycle validation and route negative witnesses. Verilator lint still exposes combinational loops, and lowering assumes validated identities.

Verilator 5.020 and Icarus 13 are installed; lint stays warnings-fatal. Simulate emitted behavior before treating compilation as correctness.
