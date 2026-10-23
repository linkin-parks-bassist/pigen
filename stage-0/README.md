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
  second dialect, or per-file choice. Production switches only when the
  structured route covers every retained feature and deletes the corresponding
  textual machinery in the same change.

The structured frontend has resolved source/token provenance and preprocessing,
shared type/declaration syntax, scopes and stable symbols, canonical data
types/shapes and exact integers, intrinsic two-stage expressions and explicit
conversions, lvalues/predicates/clock domains, one signal arena, direct
transfers, and deduplicated incidence and ownership.

### The elastic RTL vertical slice

This is the active structured work, tracked as Tasks 1–12 (Task 7,
ready-cycle validation, is deferred to Pigen 1.0).

- **Tasks 1–6 implemented.** IDs/arena, canonical types/expressions,
  owner-validated hardware constructors and collision-safe names, type and
  expression adapters, and declaration lowering (the realization/endpoint
  shapes, the BOUNDARY three-port lowering, net/variable payload realizations
  with their 1-bit valid/ready constant controls, and the four storage
  realizations, FIFO carrying its semantic depth parameter).
- **Task 8 (atomic transfers sharing one fire identity) complete.** Both
  lowering entry points (`pigen_lower_rtl_transfers`, `pigen_lower_rtl_module`)
  and the owner-level enumeration accessors (`pigen_module_processes`,
  `pigen_process_transfers`) are implemented. The full contract is a permanent
  green `rtl-lower-test` manifest: `t8-accessor`, `t8-module`, `t8-fire-identity`,
  `t8-join`, `t8-guard` (including the strengthened shared-identity assertion on
  its guard-bearing witness), `t8-ownership`, `t8-projection`, `t8-static`, and
  `t8-process-order`, each its own `contracts.json` entry.
- **Task 9 (exact ordered output) in motion.** The ordered output model's shape,
  its owner-managed append/child-nesting, its failed-nested-append
  state-invariance pin, and its exact monotonic-coverage gate are implemented and
  green: `t9-skeleton`, `t9-nesting`, `t9-failed-append`, and `t9-coverage` are
  each a permanent green `output-model-test` section. The next in-order step is
  the build walk, `pigen_build_output_model`, walking syntax child order against
  the lowering mappings; the missing-lowered-identity diagnostic follows it.
  One product decision is pending on that step: the gate's `{1, OK}` pin is not
  reachable under the emission rule (opaque spans are token-extent, so a
  structured item's own extent is not spanned by an opaque item). The rule and
  the rest of the walk are settled; the fix is either to relax that pin to a
  reachable assertion or to let a structured extent stand without an opaque span
  over it.
- **Tasks 10–12 unimplemented**: terminal SV emission (Task 10), quarantined
  composition/simulation (Task 11), and verification and accurate status
  (Task 12).

## Building and testing

Requirements: a C17 compiler, Icarus Verilog 13.0, and Verilator 5.020.
(On Ubuntu the packaged Icarus 12.0 mis-elaborates `$bits(<signal>)` widths; use
13.0.)

```sh
make            # build the pigen executable (production prototype)
make test       # build + run the structured foundation targets and smoke suite
make verify     # full regression: every example/testbench through iverilog/verilator
```

The structured foundation is eleven C test targets — `source-test`,
`preprocess-test`, `transfer-type-test`, `syntax-model-test`, `integer-test`,
`semantic-test`, `predicate-test`, `expression-resolve-test`,
`expression-use-test`, `resolve-test`, and `rtl-test` — plus `fabric-test`,
`core-language-test`, and `pipeline-test` (all exercised through `./pigen`), and
`./tests/smoke.sh`. All pass on master.

Structured-RTL and ordered-output targets (the C test binaries compile
standalone under `/tmp`):

- `make rtl-lower-test` — the elastic RTL lowering slice (Tasks 5, 6, and 8).
  Runs every present section and reports `24 passed, 0 failed` on master.
- `make output-model-test` — the Task 9 ordered output model. Runs
  `t9-skeleton`, `t9-nesting`, `t9-failed-append`, and `t9-coverage`; all four
  pass.
- `make rtl-test`, `make rtl-name-test`, and `make transfer-type-test` — the
  structured RTL arena, collision-safe naming, and transfer-type catalogue
  regressions. All green.

### The `T=` section filter

`rtl-lower-test` and `output-model-test` use a section harness. Sections are
named by task (`t5-*`, `t6-*`, `t8-*` for the lowering slice; `t9-*` for the
ordered output model). The `t5-*` sections share one semantic/RTL model and must
run together, while each `t6-*` section builds its own model. Select sections by
name prefix and `--list` prints the names:

```sh
make rtl-lower-test                      # run all present sections
make rtl-lower-test T=t8-accessor        # run a single section
make rtl-lower-test T="t6-storage t6-boundary"   # run a subset
make rtl-lower-test T=--list             # list the section names
make output-model-test T=t9-coverage     # one ordered-output section
```

The production-prototype regressions (all exercised by `make verify`):
`pipeline-test`, `pipeline-scope-test`, `pipeline-syntax-test`, `biquad-bank-test`,
`coslice-test`, `slicing-test`, `signal-syntax-test`, `validate-test`,
`signed-widen-test`, `ready-break-test`, `clear-test`, `fsm-test`, and the
example-design waveform builds (`make waveform`, `make compiler-waveform`,
`make mac-waveform`, `make biquad-waveform`, `make text-waveform`,
`make join-waveform`, `make fifo-waveform`, `make skid-waveform`,
`make skid-compare-waveform`, `make port-waveform`, `make bram-waveform`,
`make guarded-waveform`, `make output-waveform`). `make clean` removes the
built `pigen` executable.

The compiler itself is invoked as:

```sh
./pigen input.pigen -o output.sv
```

## Project layout

- `src/` — compiler source. Frontend (source, lexer, preprocess, syntax,
  type/declaration, semantic resolve), owner subsystems (`transfer`, `pipeline`,
  `fsm`, `blocks`), the structured RTL layer (`rtl`, `rtl_lower`, `rtl_name`),
  and the ordered output model (`output`). `pigen.c` is the production prototype
  entry point.
- `include/pigen/` — the C interfaces for every subsystem.
- `rtl/pigen_primitives.sv` — synthesizable storage primitives (FIFO, skid, buf,
  port) emitted alongside generated designs.
- `tests/` — C/hardware regressions: structured foundation tests (`*_test.c`),
  `.pigen` designs, and `*_tb.sv` testbenches. `contracts.json` and the shell
  drivers (`smoke.sh`, `core_language.sh`, `fabric_smoke.sh`, …) orchestrate the
  suites; each `rtl-lower-test` and `output-model-test` section is one manifest
  entry.
- `examples/` — executable prototype designs (`.pigen`) with testbenches
  (`*_tb.sv`) covering pipelines, fabrics, and the retained-core examples.
- `.knowledge/` — the project's maintained knowledge source (spec, plan,
  contracts, and answer leaves).

## Status

Pigen is pre-release. The production prototype compiles the example set and
passes `make verify`; the structured frontend's elastic RTL lowering has Task 8
complete and Task 9 in motion (its ordered output model is implemented through
the monotonic-coverage gate, with the build walk and the missing-identity
diagnostic still to land). It does not yet emit terminal SV (Task 10) and is not
attached to `./pigen`. Accepted non-Pigen SystemVerilog forms that fail to
preserve meaning are compiler defects to diagnose, not language restrictions.

Known gaps, tracked in the project knowledge tree
(`.knowledge/what/is/broken.md`):

- **Co-slice gives the last destination the whole aggregate.** For a
  multi-destination co-sliced transfer the prototype assigns the full packed
  aggregate to the last destination instead of its slice. Verilator reports it
  as `WIDTHTRUNC` warnings; `make verify` does not treat them as failures, so
  they pass silently.
- **`make verify` passes but does not fail on lint warnings.** Its Verilator
  lint runs with `-Wno-fatal`, so width warnings such as the co-slice
  truncation above pass silently. Making the lint fail on warnings is a named
  next step.
- **The elastic RTL vertical slice is incomplete.** Tasks 9 (build walk,
  missing-identity diagnostic), 10 (terminal SV emission), 11 (quarantined
  composition/simulation), and 12 (verification and accurate status) remain.
- **The structured frontend is unlinked from production `./pigen`.** The
  production `make pigen` target still builds the textual prototype, not the
  structured path.
- **`pigen_rtl_module_add` leaves a record partly uninitialized.** Only the
  `origin` is set; the owner ranges stay uninitialized (unlike the
  with-owner constructor). No production caller uses the plain form today.
