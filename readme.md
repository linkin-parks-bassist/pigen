# Pigen

Pigen is a **pre-release C17 source-to-source compiler** that extends ordinary
SystemVerilog with **ready/valid atomic transfers** and **inline pipeline,
transfer, fabric, and FSM blocks**. It reads `.pigen` sources, performs type and
semantic analysis, and emits **readable, synthesizable SystemVerilog**.

Pigen is source-to-source: it does not change simulation timing. Accepted
non-Pigen SystemVerilog must preserve widths, signedness, events, reset, and
observable cycles.

## Language

A runtime signal is a **data type × transfer type × declarator shape**. The
intended declarations are **data-first**, e.g.:

```systemverilog
int[16] fifo[8] pending[lanes];
```

Pigen primitives are two-state `int[n]`, `uint[n]`, and `bit`/`bit[n]`; ordinary
SystemVerilog `byte` is its own signed type. Transfer types are the
`wire`/`reg`/`logic` statics plus `buf`/`port`/`fifo`/`skid`, and abstract
input variables.

Every module input has payload/valid inbound and ready outbound. Unqualified
inputs are abstract; explicit annotations constrain compatible peers.

A **transfer** has one guard and one fire event across all ready destinations
and distinct valid-consuming sources. Projections consume complete base tokens,
repeated reads deduplicate, co-slices preserve packed bit-stream order, and
buffered destinations are whole. Ownership requires exclusive buffered
producers/consumers; self-consumption and unbroken ready cycles are errors.

Inline **pipeline** stages read immutable incoming fields and define outgoing
packet state (`buf`, `yield`, complete guards, reset bindings). **Fabrics**
connect resolved child instance-port identities with per-connection payload
compatibility, exclusive direct links or arbitrated routed links, and
registered-occupancy buffering. **FSM** state bodies are ordinary statements;
`goto` is guarded/terminal.

## Current implementation state

Pigen has two frontends. The **production prototype** is a quarantined
textual pipeline that compiles the full example set end to end. A separate
**structured frontend** (the data-first semantic model plus an elastic RTL
lowering slice) is being built in parallel and is quarantined from production;
no partial attachment, fallback, second dialect, or per-file choice exists yet.

The structured frontend has resolved source/token provenance and
preprocessing, shared type/declaration syntax, scopes and stable symbols,
canonical data types/shapes/exact integers, intrinsic two-stage expressions and
explicit conversions, lvalues/predicates/clock domains, one signal arena,
direct transfers, and deduplicated incidence and ownership.

The **elastic RTL vertical slice** is the active work:

- **Tasks 1–6 implemented.** IDs/arena, canonical types/expressions,
  owner-validated hardware constructors and collision-safe names, type and
  expression adapters, and declaration lowering (realization/endpoint shapes,
  the BOUNDARY three-port lowering, net/variable payload realizations, and the
  four storage realizations).
- **Task 7 (ready-cycle validation) deferred** to Pigen 1.0.
- **Task 8 (atomic transfers sharing one fire identity) in progress.** The two
  lowering entry points (`pigen_lower_rtl_transfers`,
  `pigen_lower_rtl_module`) are declared, the owner-level enumeration
  accessors are implemented, and the entry now reports 0 for the base witness.
  The active work is the **fire-identity fix**: for a zero-dependency transfer
  the fire identity must be a distinct 1-bit "true" BITS node, which
  `fire_bits_one` currently gets wrong. `rtl-lower-test` therefore fails at the
  first deliberate-red gate family (`(1) FIRE IDENTITY, base_shared`), with the
  join, repeated-projection, static-only-assignment, process-order,
  module-composition, and guard-and-dependency families staged behind it.
- **Tasks 8 (destination publication, module composition) and 9–12
  unimplemented** (exact ordered output, terminal SV emission, quarantined
  composition/simulation, and verification).

Verification of the structured foundation is all eleven C foundation targets
passing; `rtl-lower-test` compiles clean under `-Werror` and fails only at the
first deliberate-red gate, while `rtl-test`, `rtl-name-test`, and
`transfer-type-test` are green. The production prototype passes the full
`make verify` regression suite.

## Building and testing

Requirements: a C17 compiler, Icarus Verilog 13.0, and Verilator 5.020.
(On Ubuntu the packaged Icarus 12.0 mis-elaborates `$bits(<signal>)` widths;
use 13.0.)

```sh
make            # build the pigen executable
make test       # build + run the structured foundation targets and smoke suite
make verify     # full regression: every example/testbench through iverilog/verilator
```

Individual targets:

- `make pigen` — build the compiler.
- `make source-test preprocess-test transfer-type-test syntax-model-test
  integer-test semantic-test predicate-test expression-resolve-test
  expression-use-test resolve-test rtl-test fabric-test core-language-test
  pipeline-test` — the structured C foundation targets and smoke checks.
- `make rtl-lower-test` — the elastic RTL lowering slice; currently red at the
  first deliberate-red gate (Task 8 fire-identity work).
- `make rtl-name-test`, `make transfer-type-test`, `make pipeline-scope-test`,
  `make pipeline-syntax-test`, `make biquad-bank-test`, `make coslice-test`,
  `make slicing-test`, `make signal-syntax-test`, `make validate-test`,
  `make signed-widen-test`, `make ready-break-test`, `make clear-test`,
  `make fsm-test` — focused hardware regressions.
- `make waveform`, `make mac-waveform`, `make fifo-waveform`,
  `make skid-waveform`, `make biquad-waveform`, `make join-waveform`,
  `make compiler-waveform`, `make text-waveform`, `make port-waveform`,
  `make bram-waveform`, `make guarded-waveform`, `make output-waveform` —
  example design builds through the simulators (waveform traces to `.vcd`).
- `make clean` — remove the built `pigen` executable.

The compiler itself is invoked as:

```sh
./pigen input.pigen -o output.sv
```

## Project layout

- `src/` — compiler source. Frontend (lexer, source, preprocess, syntax,
  type/declaration, semantic resolve), owner subsystems (`transfer`,
  `pipeline`, `fsm`, `blocks`), and the RTL layer (`rtl`, `rtl_lower`,
  `rtl_name`). `pigen.c` is the production prototype entry point.
- `include/pigen/` — the C interfaces for every subsystem.
- `rtl/pigen_primitives.sv` — synthesizable storage primitives (FIFO, skid,
  buf, port) emitted alongside generated designs.
- `tests/` — C/hardware regressions: structured foundation tests (`*_test.c`),
  `.pigen` designs, and `*_tb.sv` testbenches. `contracts.json` and the shell
  drivers (`smoke.sh`, `core_language.sh`, `fabric_smoke.sh`, ...) orchestrate
  the suites.
- `examples/` — executable prototype designs (`.pigen`) with testbenches
  (`*_tb.sv`) covering pipelines, fabrics, and the retained-core examples.
- `.knowledge/` — the project's maintained knowledge source (spec, plan,
  contracts, and answer leaves).

## Status

Pigen is pre-release. The production prototype compiles the example set and
passes `make verify`; the structured frontend's elastic RTL lowering is
in progress on Task 8 and does not yet publish destinations or modules.
Accepted non-Pigen SystemVerilog forms that fail to preserve meaning are
compiler defects to diagnose, not language restrictions.
