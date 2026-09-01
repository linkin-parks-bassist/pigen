# Elastic RTL Vertical Slice Design

**Author:** Ada, 2026-09-01

## Purpose

This cutover establishes the first complete structured route from written
source to emitted SystemVerilog:

```text
source and preprocessing
    -> syntax
    -> semantic resolution and validation
    -> elastic RTL lowering
    -> ordered compilation output
    -> SystemVerilog emission
```

It resumes the architecture fixed by `SPEC.md`, `PLAN.md`,
`agent_notes/COMPILER_ARCHITECTURE.md`, and
`agent_notes/SEMANTIC_INVARIANTS.md`. It does not introduce a second language
or preserve superseded prototype syntax.

The first vertical slice covers one resolved module, its signal declarations,
one-edge clocked processes, structured guards, and direct atomic transfers.
The infrastructure must already have the final ownership boundaries needed by
pipelines, FSMs, instances, and fabrics, but it must not speculate about their
concrete semantics.

## Fixed boundaries

The semantic model remains the sole owner of signals, transfer incidence,
types, shapes, conversions, predicates, domains, and ownership decisions.
Lowering receives only validated semantic identities. It does not inspect
syntax spelling or source text.

Elastic RTL IR owns resolved hardware objects:

- modules and boundary ports;
- payload, valid, and ready endpoints;
- nets and procedural variables;
- primitive instances and their parameter and port connections;
- combinational equations;
- clocked processes and ordered guarded updates; and
- RTL expressions whose widths, signedness, conversions, and projections are
  already explicit.

The RTL IR does not contain semantic source spellings or opaque SystemVerilog.
Every object retains source provenance and, where applicable, the semantic
identity from which it was lowered.

Unsupported ordinary SystemVerilog remains outside the semantic and RTL
models. An ordered compilation-output model contains either an opaque written
source span or a structured module-layout identity. A module layout in turn
orders opaque module-item spans and references to structured RTL objects. The
terminal emitter copies opaque spans byte-for-byte and renders structured RTL.
It never searches, parses, or infers meaning from an opaque span. This output
model is compilation layout, not a second semantic representation.

The final-backend requirement that RTL IR be the only input to generated
Pigen SystemVerilog concerns structured items. The compilation-output model
may continue to carry ordinary opaque SystemVerilog so long as it is never a
semantic side channel.

## RTL IR organization

`pigen_rtl_model` is an arena model with distinct identity types. Each arena
record is immutable after construction except during a private builder
transaction. Public constructors validate referenced identities and reject
partially formed objects.

The model has separate records for:

- `pigen_rtl_module`: ordered ports, declarations, instances, equations, and
  processes;
- `pigen_rtl_object`: port, net, or variable identity with type, direction,
  provenance, and generated-name request;
- `pigen_rtl_instance`: primitive/module identity, parameters, and ordered
  connections;
- `pigen_rtl_expression`: literals, references, unary and binary operations,
  concatenations, indexing, selection, conditional expressions, and explicit
  conversions;
- `pigen_rtl_equation`: one combinational destination and value;
- `pigen_rtl_process`: one clock, edge, and ordered update range; and
- `pigen_rtl_update`: destination, value, and explicit enable expression.

The IR type record carries the emitted packed layout, signedness, and state
domain. Symbolic widths and bounds retain constant-expression identities
lowered into RTL expressions; the emitter does not recompute them.

Names are requests, not identities. A request contains a source-derived stem
or an internal role. One module-local allocator assigns collision-safe final
names before emission and records the mapping in RTL IR. No later lookup uses
the assigned text.

## Lowering adapters

Lowering is split by ownership boundary rather than source feature.

The data-type adapter lowers canonical data types and shapes into RTL type and
declarator records. It is the only lowering code that queries data-type layout,
state domain, signedness, or conversion records.

The transfer-realization adapter queries
`pigen_transfer_type_descriptor.realization`, then queries the corresponding
realization descriptor. It switches on backend-neutral realization identity,
never on `buf`, `port`, `fifo`, or another source transfer type. It creates the
payload/control endpoints and storage primitive instance appropriate to the
realization descriptor.

The expression adapter recursively lowers already typed semantic expressions.
Every explicit semantic conversion produces an explicit RTL conversion. The
adapter may memoize by semantic expression identity; it does not resolve names,
types, or widths.

The transfer adapter derives one fire expression from the transfer's guard,
destination readiness, and distinct consuming-source validities. It emits all
destination updates and source-ready equations from that one identity. Static
valid and ready laws lower through descriptor constants. Repeated reads and
projections share the transfer's deduplicated incidence entries.

## Ready-dependency validation

Semantic validation gains a whole-compilation-unit ready-dependency graph
before RTL lowering.

Each graph vertex is a signal control endpoint. An edge means that one ready
value depends combinationally on another. The realization descriptor decides
whether a signal propagates downstream ready or breaks the chain; lowering
does not duplicate this policy. Direct transfers contribute edges from their
deduplicated incidence and destination set.

The validator rejects every strongly connected component containing a cycle
and no deliberate ready-chain break. The diagnostic is anchored at a transfer
which closes the cycle and reports the participating signal declarations as
secondary provenance when the diagnostic interface supports it. A self-loop
is the one-vertex case of the same rule, not a separate textual check.

## Compilation output and ordinary SystemVerilog

The frontend constructs ordered compilation-unit and module layouts covering
every byte of each written source file exactly once. Opaque items refer to
immutable source spans. Structured items refer to RTL identities and replace
the complete source extent owned by those objects. Layout nesting, separators,
and untouched module items remain source-boundary facts; they do not enter RTL
IR.

A module can become structured only when every Pigen-relevant construct inside
it is represented through syntax and semantics. Unrecognized ordinary
SystemVerilog inside an otherwise structured module remains an ordered opaque
module item if it has no Pigen dependency. If the compiler would need to
inspect that item to lower a Pigen construct, compilation rejects the construct
at its original span.

Preprocessor provenance is retained for diagnostics. Opaque output uses the
written view; structured constructs use the expanded view for meaning and
their origin chains for diagnostics. Macro expansion is never reconstructed
by rendering expanded tokens back into source.

## Production cutover

The structured route is developed behind focused unit and integration tests;
it is not linked into `./pigen` as a validator or alternate production path.
The executable switches only when the structured route covers the production
surface being retained at that point.

At the switch:

- data-first declarations are the only accepted Pigen declaration syntax;
- the structured emitter becomes authoritative for all retained Pigen core
  transfers;
- the corresponding declaration, assignment, guard, width, ownership, and
  emission scanners are deleted;
- tests and examples using superseded Pigen syntax are replaced, not retained;
  and
- ordinary SystemVerilog pass-through behavior remains a release gate.

If pipelines, FSMs, or fabrics have not yet migrated when the core route is
ready, the production switch waits. The executable does not route those
features through textual preprocessing before feeding generated text into the
structured frontend, and it does not choose between old and new compilers per
file. The cutover branch may contain an unlinked structured implementation,
but every delivered production state has one compiler path.

## Diagnostics and failure atomicity

Syntax errors use origin and written-source provenance already owned by the
frontend. Semantic, graph, and lowering errors use the source span stored on
the owning semantic or RTL object. The emitter has no semantic failure modes
beyond malformed IR, allocation failure, and output I/O failure.

Every builder which appends to multiple arenas is transactional. On failure it
restores all affected counts. Failed syntax, semantic, graph, and lowering
operations leave no reachable partial object.

Generated-width policy is enforced before lowering. Known incompatible widths,
shapes, or signedness fail semantically. Parameter-dependent legal widths
produce explicit constraints or elaboration assertions from semantic records;
the emitter never invents a `$bits` comparison by comparing rendered text.

## Verification

Focused structural tests prove:

- all RTL references use identities and survive colliding source names;
- lowering enumerates realization identities rather than source transfer
  types;
- explicit conversions, projections, and concatenation order survive into IR;
- transfer incidence yields one fire identity and deduplicated valid/ready
  equations;
- ready cycles are rejected across multiple transfers and accepted when a
  realization deliberately breaks the chain;
- opaque SystemVerilog is copied exactly and never searched for Pigen meaning;
- macro-origin diagnostics identify written source; and
- every multi-arena failure rolls back atomically.

Behavioral integration tests compile current data-first core designs, simulate
the emitted RTL under backpressure, and cover static transfers, elastic slots,
pulse registers, parameterized FIFOs, skid queues, joins, projections,
concatenations, co-slices, guards, stalls, simultaneous push/pop, and reset.

Compatibility tests compare representative pure and mixed SystemVerilog before
and after Pigen processing. The final cutover runs `make verify` with warnings
as errors and confirms that no marker comments, generated-name lookups,
rewritten-source passes, prototype transfer descriptors, or core textual
scanners remain.

## Deferred work

Pipelines, FSMs, child instances, and fabrics use this IR and output boundary
when their ordered migration phases begin. Their semantic identities and
feature-specific lowering are not part of this vertical-slice design.

The later language features listed in `PLAN.md` remain deferred. This design
does not settle stage-body transfers, `stall()`, pipeline `export`, fabric
priority controls, struct-like instance syntax, or additional data-type
families.
