# Pigen

Pigen is a **pre-release C17 source-to-source compiler** that extends ordinary
SystemVerilog with **ready/valid atomic transfers** and **inline pipeline,
fabric, and FSM blocks**. It reads `.pigen` sources, performs type and semantic
analysis, and emits readable, synthesizable SystemVerilog. Because it is
source-to-source it does not change simulation timing: accepted non-Pigen
SystemVerilog must preserve widths, signedness, events, reset, and observable
cycles.

## Language

A runtime signal is a **data type × transfer type × declarator shape**, declared
data-first, e.g.:

```systemverilog
int[16] fifo[8] pending[lanes];
```

Pigen primitives are the two-state `int[n]`, `uint[n]`, and `bit`/`bit[n]`;
ordinary SystemVerilog `byte` is its own signed type. Transfer types are the
`wire`/`reg`/`logic` statics plus `buf`/`port`/`fifo`/`skid`, and abstract input
variables.

Every module input has payload/valid inbound and ready outbound; unqualified
inputs are abstract and explicit annotations constrain compatible peers. A
**transfer** has one guard and one fire event across all ready destinations and
distinct valid-consuming sources. Projections consume complete base tokens,
repeated reads deduplicate, co-slices preserve packed bit-stream order, and
buffered destinations are whole. Ownership requires exclusive buffered
producers/consumers; self-consumption and unbroken ready cycles are errors.

Inline **pipeline** stages read immutable incoming fields and define outgoing
packet state (`buf`, `yield`, complete procedural guards, reset bindings).
**Fabrics** connect resolved child instance-port identities with per-connection
payload compatibility, exclusive direct links or arbitrated routed links, and
registered-occupancy buffering. **FSM** state bodies are ordinary statements;
`goto` is guarded and terminal and never implicitly waits for a transfer.

## Current implementation state

Pigen has two frontends:

- A **production prototype**: a quarantined textual pipeline that compiles the
  full example set end to end and passes the full `make verify` regression
  suite. This is what `make pigen` builds today.
- A **structured frontend**: the data-first semantic model plus an elastic RTL
  lowering slice, built in parallel and quarantined from production. It has not
  been attached to `./pigen` yet, and there is no partial attachment, fallback,
  second dialect, or per-file choice.

The structured frontend has resolved source/token provenance and preprocessing,
shared type/declaration syntax, scopes and stable symbols, canonical data
types/shapes and exact integers, intrinsic two-stage expressions and explicit
conversions, lvalues/predicates/clock domains, one signal arena, direct
transfers, and deduplicated incidence and ownership.

### The elastic RTL vertical slice

This is the active structured work, tracked as Tasks 1–12:

- **Tasks 1–6 implemented.** IDs/arena, canonical types/expressions,
  owner-validated hardware constructors and collision-safe names, type and
  expression adapters, and declaration lowering (the realization/endpoint
  shapes, the BOUNDARY three-port lowering, net/variable payload realizations
  with their 1-bit valid/ready constant controls, and the four storage
  realizations, FIFO carrying its semantic depth parameter).
- **Task 7 (ready-cycle validation) deferred** to Pigen 1.0.
- **Task 8 (atomic transfers sharing one fire identity) in progress.** The two
  lowering entry points (`pigen_lower_rtl_transfers`, `pigen_lower_rtl_module`)
  are declared, the owner-level enumeration accessors are implemented, and the
  entry reports `0` for the base witness. The **fire-identity family is
  implemented and passing**: a zero-dependency transfer's fire identity is the
  distinct non-constant 1-bit "true" BITS node, and a successful lowering prints
  no diagnostic traces. The remaining gate families — **join, repeated
  projection, static-only assignment, process order, module composition, and
  guard-and-dependency** — are each a separate section of `tests/rtl_lower_test.c`
  and its own `rtl-lower-test` manifest entry; each is red until its lowering
  lands and each is absent from the file (and the manifest) until its test
  packet lands. `rtl-lower-test` therefore runs fully green on master today.
- **Tasks 9–12 unimplemented**: exact ordered output (Task 9), terminal SV
  emission (Task 10), quarantined composition/simulation (Task 11), and
  verification and accurate status (Task 12).

Verification of the structured foundation is all eleven C foundation targets
passing, `rtl-lower-test` clean under `-Werror` at 16 passed / 0 failed, and
`rtl-test`, `rtl-name-test`, and `transfer-type-test` green. The production
prototype passes the full `make verify` regression suite.

## Building and testing

Requirements: a C17 compiler, Icarus Verilog 13.0, and Verilator 5.020.
(On Ubuntu the packaged Icarus 12.0 mis-elaborates `$bits(<signal>)` widths; use
13.0.)

```sh
make            # build the pigen executable (production prototype)
make test       # build + run the structured foundation targets and smoke suite
make verify     # full regression: every example/testbench through iverilog/verilator
```

Structured-foundation and structured-RTL targets (the C test binaries compile
standalone under `/tmp`):

- `make rtl-lower-test` — the elastic RTL lowering slice. Runs every present
  section and reports `16 passed, 0 failed` on master.
- `make rtl-test`, `make rtl-name-test`, `make transfer-type-test` — the
  structured RTL arena, collision-safe naming, and transfer-type catalogue
  regressions. All green.
- The other C foundation targets: `source-test`, `preprocess-test`,
  `syntax-model-test`, `integer-test`, `semantic-test`, `predicate-test`,
  `expression-resolve-test`, `expression-use-test`, `resolve-test`.

### The `T=` section filter

`rtl-lower-test` uses a section harness. Each section is named by task
(`t5-*`, `t6-*`, `t8-*`); the `t5-*` sections share one semantic/RTL model and
must run together, while each `t6-*` section builds its own model. Select
sections by name prefix and `--list` prints the names:

```sh
make rtl-lower-test                      # run all present sections
make rtl-lower-test T=t8-accessor        # run a single section
make rtl-lower-test T="t6-storage t6-boundary"   # run a subset
make rtl-lower-test T=--list             # list the section names
```

The production-prototype regressions (all exercised by `make verify`):
`pipeline-test`, `pipeline-scope-test`, `pipeline-syntax-test`, `biquad-bank-test`,
`coslice-test`, `slicing-test`, `signal-syntax-test`, `validate-test`,
`signed-widen-test`, `ready-break-test`, `clear-test`, `fsm-test`, and the
example-design waveform builds (`make waveform`, `make mac-waveform`,
`make biquad-waveform`, `make join-waveform`, `make fifo-waveform`,
`make skid-waveform`, `make port-waveform`, `make bram-waveform`,
`make guarded-waveform`, `make output-waveform`). `make clean` removes the
built `pigen` executable.

The compiler itself is invoked as:

```sh
./pigen input.pigen -o output.sv
```

## Project layout

- `src/` — compiler source. Frontend (source, lexer, preprocess, syntax,
  type/declaration, semantic resolve), owner subsystems (`transfer`, `pipeline`,
  `fsm`, `blocks`), and the structured RTL layer (`rtl`, `rtl_lower`,
  `rtl_name`). `pigen.c` is the production prototype entry point.
- `include/pigen/` — the C interfaces for every subsystem.
- `rtl/pigen_primitives.sv` — synthesizable storage primitives (FIFO, skid, buf,
  port) emitted alongside generated designs.
- `tests/` — C/hardware regressions: structured foundation tests (`*_test.c`),
  `.pigen` designs, and `*_tb.sv` testbenches. `contracts.json` and the shell
  drivers (`smoke.sh`, `core_language.sh`, `fabric_smoke.sh`, …) orchestrate the
  suites; each `rtl-lower-test` section is one manifest entry.
- `examples/` — executable prototype designs (`.pigen`) with testbenches
  (`*_tb.sv`) covering pipelines, fabrics, and the retained-core examples.
- `.knowledge/` — the project's maintained knowledge source (spec, plan,
  contracts, and answer leaves).

## Status

Pigen is pre-release. The production prototype compiles the example set and
passes `make verify`; the structured frontend's elastic RTL lowering is in
progress on Task 8 and does not yet publish destinations or modules (Task 8's
module entry still stubs `PIGEN_INVALID_ID`). Accepted non-Pigen SystemVerilog
forms that fail to preserve meaning are compiler defects to diagnose, not
language restrictions. Known open defects are tracked in the project knowledge
tree (`.knowledge/what/is/broken.md`).
