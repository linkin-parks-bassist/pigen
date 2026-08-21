# Pigen implementation plan

This tracked plan is the current execution order. `SPEC.md` is authoritative
for the target language; `agent_notes/SEMANTIC_INVARIANTS.md` and
`agent_notes/signal_model.md` are authoritative for the replacement compiler
middle. Completed prototype behavior is evidence, not architecture.

## Policy

- Repair the compiler architecture before adding post-v1 features.
- Every pass communicates through structured identities and records. Strings
  exist at source ingestion and SystemVerilog emission, not between passes.
- A structured slice becomes authoritative only when its textual predecessor
  is deleted in the same change. Never add a shadow validator, fallback,
  compatibility path, or second Pigen dialect.
- Superseded Pigen behavior and syntax receive no preservation tests. Replace
  their tests at each clean break.
- Ordinary SystemVerilog compatibility is permanent and distinct. Compatibility
  tests must prove that processing accepted SV does not change observable
  behavior; a silent change is release-blocking.
- Every consequential semantic choice which is not fixed by `SPEC.md` stops for
  David's decision before implementation.

## Fixed semantic foundation

- Every runtime datum is a signal: data type × transfer type × declarator
  shape.
- Every signal has a transfer type. A transfer type may be concrete or abstract
  but is never absent.
- `wire`, `reg`, and `logic` are static transfer types. Their constant transfer
  laws are ordinary members of the same algebra as `buf`, `port`, `fifo`, and
  `skid`.
- Every module input exposes payload, valid, and ready. A unqualified input has an
  abstract transfer type; an explicit transfer type constrains compatible
  connections without changing the receiving body's uniform handshake model.
- Static ready/valid laws may lower to constants and disappear from emitted RTL
  where context permits. They remain explicit in semantic analysis.
- `byte` is an unsigned eight-bit bit-vector, not an integer.
- A net is a SystemVerilog realization category, not Pigen's umbrella semantic
  object. Backend storage and wiring choices never replace signal identity.

## Current architecture status

The replacement foundation has immutable source storage, preprocessing with
token provenance, a partial structured syntax tree, scopes, stable identities,
structural packed types, typed expressions and lvalues, canonical predicates,
clock domains, direct transfers, and a transfer-incidence ownership graph. Its
focused unit tests pass. Static and general declarations now resolve into one
signal arena and one symbol binding; every direct-transfer incidence is
recorded, with transfer-type laws deciding semantic roles and domain behavior.
The full `make verify` suite passed after the unified-signal, canonical-shape,
and structured declarator-shape cutovers on 2026-08-21.

It is not linked into the production executable. The production compiler still
uses rewritten source, generated names, marker comments, rescanning, and
feature-local models. Canonical structural shape identities are shared by
signals and expressions, and recognized declarations now parse ordered count
and range dimensions directly into those identities. Expression indexing
consumes unpacked dimensions before packed dimensions; unsupported unpacked
slices and concatenations are rejected rather than reinterpreted. The target
data-first declaration grammar remains incomplete. Storage/lowering laws are
not yet fully modeled. There is no elastic RTL IR or terminal structured
emitter.

## Architecture cutover

### 1. Unify the semantic signal model

- [x] Replace the separate static and general signal identities with one
  signal arena and one symbol binding.
- [x] Represent data type, concrete or abstract transfer type, declarator shape,
  direction, provenance, and transfer-type parameters independently.
- [ ] Centralize transfer-type laws for validity, readiness, storage,
  consumption, production, ownership, domain binding, and lowering constants.
- [x] Make expression-use and transfer-incidence analysis record every signal,
  including statics, and apply behavior through transfer-type laws.
- [x] Remove the superseded terminology from source, tests, filenames,
  diagnostics, documentation, and generated comments.

### 2. Complete the shared frontend

- [ ] Parse target data-first declarations and declaration shapes structurally.
- [ ] Preserve ordinary SystemVerilog declarations without reinterpreting
  explicit ranges or unrelated expression indexing.
- [ ] Represent generic input boundaries and explicit transfer-type constraints.
- [ ] Complete parameter, type, aggregate, array, and expression forms required
  by accepted Pigen constructs; reject an unsupported form at its original
  span rather than scanning opaque text.
- [ ] Finish macro concatenation, stringification, and required argument forms
  without weakening token provenance.

### 3. Establish one complete vertical lowering slice

- [ ] Lower one module, its declarations, one clocked process, and direct atomic
  transfers through syntax, resolution, semantic validation, incidence graph,
  elastic RTL IR, and SystemVerilog emission.
- [ ] Move contextual sizing, signedness, aggregate-width checks, and mismatch
  diagnostics before RTL lowering.
- [ ] Build the whole-unit ready-dependency graph and reject every indirect
  combinational cycle which crosses no deliberate ready-chain break.
- [ ] Match current atomicity, projection, concatenation, co-slicing, validity
  action, domain, ownership, stall, and throughput behavior.
- [ ] Delete the corresponding production textual scanners and emitters when
  the structured slice becomes authoritative.

### 4. Migrate pipelines

- [ ] Represent pipeline, stage, travelling field, immutable incoming packet,
  outgoing update, external dependency, reset binding, and yield identities in
  the common model.
- [ ] Lower pipelines structurally into their parent module through the common
  incidence graph and elastic RTL IR.
- [ ] Reproduce guarded execution, private stage scopes, shadow diagnostics,
  external atomic inputs, reset behavior, stage repartitioning, and one-item-per-
  cycle operation.
- [ ] Replace prototype pipeline syntax and tests with the target data-first
  surface in one clean break.
- [ ] Verify the biquad bank as the decisive pipeline integration target, then
  delete the pipeline rewriting subsystem.

### 5. Migrate FSMs

- [ ] Represent state, initial state, transition, priority, guard, and action
  identities through shared scopes, expressions, signals, domains, and
  transfers.
- [ ] Lower FSM control into the common RTL IR and delete textual FSM lowering.

### 6. Migrate instances and fabrics

- [ ] Resolve child-module instances and their port signals before fabric
  analysis; keep the future struct-like instance spelling independent of those
  identities.
- [ ] Replace top-level fixed-width fabric units with inline module-owned fabric
  blocks and two-component `instance.port` endpoints.
- [ ] Infer and validate payload compatibility per connection; remove the shared
  `PAYLOAD_W` interface.
- [ ] Preserve blind endpoints, exclusive direct links, many-to-one routed
  arbitration, deterministic balanced topology, compact relative routes,
  reachability proofs, buffered ready-path breaks, manifests, and SVG output.
- [ ] Derive topology, routes, RTL, diagnostics, and SVGs from one fabric model,
  then delete top-level fabric parsing and textual block lowering.

### 7. Finish the backend and remove the prototype

- [ ] Make elastic RTL IR the only input to SystemVerilog emission.
- [ ] Centralize collision-safe generated-name allocation and retain provenance
  from emitted objects to semantic objects.
- [ ] Remove every remaining marker comment, generated-name lookup, rewritten-
  source pass, semantic string comparison, and duplicate feature resolver.
- [ ] Remove prototype-only syntax, examples, tests, APIs, and files.
- [ ] Compact architecture notes to durable invariants and current status.

## SystemVerilog compatibility work

- [ ] Build a representative pure-SV pass-through corpus covering modules,
  interfaces, packages, classes, generate blocks, procedural forms, sizing,
  signedness, timing controls, assertions, arrays, and preprocessing.
- [ ] Compare simulation behavior before and after Pigen processing.
- [ ] Add mixed-source compatibility cases proving that a Pigen construct does
  not reinterpret unrelated SystemVerilog in the same file or design.
- [ ] Audit every ordinary-SV rejection: specify and diagnose deliberate
  restrictions; fix accidental restrictions.

## Specified later language work

These features are named by `SPEC.md` but remain deliberately unimplemented
until the architecture cutover is complete and their open semantics are fixed:

- stage-body transfer operations and explicit `stall();`;
- `export` of an earlier pipeline stage;
- fabric priority and topology controls;
- a clean struct-like child-instantiation surface;
- additional scalar, aggregate, enum, and user-defined data types.

## Preserved ideas not yet specified

These ideas from the previous plan are retained so they are not silently lost.
They are not implementation tasks until David chooses semantics and adds them
to `SPEC.md` where language-visible:

- packet-field liveness pruning with auditable conservative retention;
- optional explicit pipeline reset blocks beyond the specified reset bindings;
- a reusable payload-directed splitter component;
- an `objective` control for deterministic fabric topology optimization;
- configurable router and endpoint depths;
- optional loss monitors for unaccepted `port` pulses;
- synthesis checks for names, ready paths, inferred storage, and representative
  FPGA and ASIC toolchains.

## Release gates

- [ ] The production executable contains one structured compiler path and no
  semantic textual side channels.
- [ ] `make verify` passes from a clean tree with warnings treated as errors.
- [ ] Pure and mixed SystemVerilog compatibility tests preserve observable
  behavior.
- [ ] Every accepted construct is specified and has behavioral, diagnostic,
  backpressure, atomicity, and throughput coverage appropriate to it.
- [ ] Every deliberate SystemVerilog restriction is specified and diagnosed at
  its original source location.
- [ ] Generated SystemVerilog and fabric SVGs are deterministic for identical
  inputs and options.
