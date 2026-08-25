# Compiler architecture

## Diagnosis

The production compiler is a successful semantic prototype, not the intended
architecture. Rewritten source currently acts as its intermediate
representation. Meaning is repeatedly recovered through token scans, copied
substrings, textual comparisons, generated suffixes, byte offsets, and marker
comments. This makes scope, types, control, ownership, and feature interaction
fragile even when behavioral tests pass.

Strings belong at two boundaries:

```text
source text -> frontend
backend -> emitted SystemVerilog
```

They must not carry meaning between compiler passes.

## Target spine

The compiler has one forward progression:

```text
immutable sources and preprocessing
    -> lossless written view + expanded token view
    -> structured syntax
    -> scopes, symbols, data types, transfer types, and shapes
    -> typed expressions, lvalues, predicates, and clock domains
    -> semantic signals, transfers, pipelines, FSMs, and fabrics
    -> signal-incidence, ownership, and ready-dependency graphs
    -> elastic RTL IR
    -> SystemVerilog and fabric SVG emission
```

Every arrow consumes structured data and produces structured data. Unsupported
SystemVerilog remains lossless opaque syntax only outside Pigen's semantic
boundary. Opaque syntax is never searched for semantic dependencies.

The language model is fixed in `signal_model.md`: every runtime datum is one
signal with a data type, concrete or abstract transfer type, and declarator
shape. Ordinary SystemVerilog nets and variables are not a parallel species.

## Shared services

The compiler middle owns one implementation of each concern:

- immutable source files, line tables, spans, and token-origin provenance;
- preprocessing and written/expanded source views;
- arenas and stable typed identities;
- syntax structure and explicit opaque boundaries;
- scopes, symbols, lookup, and bidirectional semantic bindings;
- data-type interning, signedness, dimensions, and symbolic width algebra;
- concrete and abstract transfer types and their behavioral laws;
- structural expressions, lvalues, and contextual typing;
- expression-use traversal and deduplicated signal incidences;
- canonical control predicates and mutual-exclusion proofs;
- clock domains, ownership, and ready-dependency validation;
- collision-safe generated names and terminal emission.

## Global shape, local knowledge

David's architectural criterion is ease of coherent experimentation. Pigen's
primitive catalogue and language surface will change repeatedly; the compiler
must make those changes local without pretending that genuinely new semantics
are free. A subsystem knows the global shape of its inputs and outputs while
remaining ignorant of concrete cases owned elsewhere.

For data types, canonical `pigen_data_type_id` values are the link between
layers, but identity alone is insufficient. One data-type subsystem must own
representation, width, signedness, state domain, numerical interpretation,
compatibility, conversion, operator typing, and lowering-facing semantics.
Expression resolution asks that subsystem what an operation means and stores
the resolved result type, conversions, and operation semantics. Later passes
consume those decisions; they do not switch independently over `int`, `uint`,
`byte`, or future fixed-point constructors.

`include/pigen/data_type.h` and `src/data_type.c` now own the primitive
catalogue, canonical interning, aliases, packed layout and projection, symbolic
width algebra, state and numerical domains, conversion policy, and unary,
binary, and conditional operation resolution. Shared type syntax resolves
`int[n]`, `uint[n]`, `bit`, and `byte` in casts and typedefs; recognizing them
at the start of data-first signal declarations remains the next syntax slice.
A compile-time descriptor table owns shared primitive facts; parameterized and
operation-specific rules remain ordinary private code. Constructor tags, raw
interning, descriptors, and canonical records are private to `src/data_type.c`;
other layers carry opaque identities and conversion/operation records.

Expression construction has no expected-result input. The data-type owner
returns complete intrinsic operation and conversion decisions; a temporary
analyzed-expression arena validates the whole tree before one postorder
materializer appends semantic expressions. Every non-identity conversion is an
explicit semantic node and every identity conversion disappears. Syntax still
retains an unclassified base token, aliases retain resolved targets, and
general semantic passes do not enumerate primitive constructors. Do not extend
unrelated passes when changing `int`, `uint`, `byte`, or a future fixed-point
family.

The descriptor table is a catalogue, not a promise that all primitive meaning
is tabular. Operation-specific rules remain ordinary code inside the data-type
owner. Parameterized families such as `int[n]`, `uint[n]`, and future
fixed-point types will need constructor-specific representation and arithmetic
rules there, while unrelated expression walkers and feature passes continue to
consume the same opaque queries and resolved decisions. Adding a fixed-width
primitive should normally add one descriptor plus only the semantics genuinely
unique to that primitive. The internal 32-bit type used for unsized integer
expressions is named `unsized_integer` specifically to keep it distinct from
the planned source-level `int[n]` family.

The practical architecture test is a hypothetical primitive change. Its
necessary edits should be confined to the data-type subsystem, source-spelling
and backend boundary mappings where applicable, specification, and focused
tests. If predicates, expression walkers, transfers, pipelines, FSMs, or
fabrics require primitive-specific branches, stop and repair the boundary.
This rule generalizes: every varying catalogue should have one owner, and other
layers should depend on its laws or capabilities rather than its members.

This is deliberately not a runtime registry or a universal plugin system. Use
small compile-time tables, tagged structural records, and centralized functions
where they express actual variation. The goal is the right abstraction level:
global structure, contained detail, and no duplicated self-knowledge.

Semantic operation identity is another shared axis. `operation.h` and
`operation.c` own the unary, binary, and select-operation vocabulary. Syntax
resolution maps written operators into that algebra; semantic expressions carry
it; the data-type subsystem supplies operand-dependent meaning. Neither source
spelling nor primitive constructors own the shared operation vocabulary.

An operator is only the shared algebraic symbol. The data-type subsystem
resolves it into required conversions plus an operation whose effective operand
identities are exactly those conversion targets. Runtime and canonical constant
expressions carry the operation record, never a raw operator plus an
independently asserted result type. Fixed-point scaling and other real lowering
semantics may extend conversion records without changing tree topology or
teaching expression walkers the primitive catalogue.

Typed boundaries are separate from intrinsic expression meaning. Assignment
resolves the destination first, resolves the RHS without destination context,
then asks the target family for one final conversion. Same-family integer
narrowing and widening are ordinary resize decisions; interpretation changes
and numerical/raw crossings require an explicit cast. `pigen_transfer_add()`
requires final data-type and shape identity independently of resolver checks.

Resource policy is likewise outside the data-type owner. The type algebra
retains exact derived widths. Expression resolution receives a positive
`maximum_generated_bits`, rejects a known excessive left-shift or power at its
operator, and records a provenance-bearing semantic constraint when the width
depends on parameters. Never clamp a lossless result to satisfy policy.

Literal interpretation is an explicit analyzer input, separate from
constant-only admissibility. Pigen runtime expressions and arguments of Pigen
integer spellings/aliases select the exact domain. Ordinary SystemVerilog
parameters, packed ranges, and SystemVerilog-family aliases select the
SystemVerilog domain. Resolve the type spelling or recursively underlying alias
owner before walking its arguments; notably, a `byte` alias remains in the
Pigen domain. Never infer literal policy from constant-only admissibility.

For known numerical ranges, the data-type owner computes range endpoints with
the arbitrary-precision integer catalogue and chooses the narrowest containing
integer type. When a declared width dominates the stored exact operand, proven
bit-length formulae produce the same result without materializing `2^width`;
semantic work must never allocate storage proportional to a merely declared
hardware width. An exact/concrete operation whose concrete width is parametric
stores one canonical numerical-range-width node containing the operator,
concrete family and width, exact value, operand order, and uniformly lossless
result family. The node evaluates the same endpoint law once parameters are
known instead of falling back to a magnitude approximation. Power keeps this
law even when its exact exponent exceeds host `size_t`; one-bit bases resolve
immediately from exponent parity, including after symbolic specialization.
Width-independent identities such as `x + 0`, `x * 1`, and `x * 0` canonicalize
before any parametric range node. Semantic conversion constructors call back
into the data-type owner and symbolic unsigned-to-signed promotion is accepted
only when the target width structurally proves the required extra sign bit.

Pipelines, transfers, FSMs, and fabrics consume these services and produce
common semantic objects. No feature privately reparses names, expressions,
types, guards, or generated text.

## Semantic-to-RTL boundary

Semantic validation finishes before realization begins. Semantic objects state
what the program means; elastic RTL IR states how that meaning is implemented
using ports, nets, registers, instances, equations, and procedural updates.

Static transfer laws may become constant ready/valid ties or disappear through
ordinary lowering optimization. The semantic signal and its transfer type
remain present before that boundary. A backend storage element likewise does
not create or redefine source-level signal identity.

The SystemVerilog backend only renders RTL IR. It performs no scope lookup,
type inference, ownership analysis, source parsing, or semantic discovery.
Fabric RTL, routes, manifests, reachability evidence, and SVGs derive from one
resolved topology model.

## Current replacement foundation (2026-08-25)

The unlinked replacement modules already provide:

- immutable source storage and binary-searched line provenance;
- raw written-source and preprocessed expanded-token views;
- macro invocation, definition, formal, and actual-token origin chains;
- conditional compilation and recursive include provenance;
- a partial hierarchical syntax tree with explicit opaque nodes;
- ordered declarator count/range dimensions resolved to canonical shapes;
- source-order parameters, typedefs, ordinary declarations, and prototype
  `buf`/`port`/`fifo`/`skid` declarations;
- scopes, symbols, stable identities, and structural packed data types;
- canonical structural shape identities shared by signals and expressions;
- a shared expression parser with structural operators, concatenations,
  indexing, and part selects; unpacked indexing consumes canonical shape
  dimensions before packed indexing applies;
- canonical constant-expression DAGs and symbolic width sums, products, and
  maxima;
- a dedicated data-type interface and implementation owning canonical type
  construction, aliases, packed layout, projection, width, state and numerical
  domains, concatenation, conversions, and operation resolution;
- one private compile-time primitive descriptor table supplying source
  spelling, fixed base width, state domain, and capabilities to those queries;
- opaque `pigen_data_type_id` values outside that owner; constructor tags, raw
  interning, and concrete canonical records are not public compiler vocabulary;
- resolved alias targets carried in canonical alias records, avoiding later
  symbol-table lookup or reinterpretation;
- a shared semantic operation algebra, distinct from syntax spelling and from
  operand-dependent data-type rules;
- resolved unary, binary, and conditional decisions containing conversions plus
  operation records, with effective operand and result types supplied once by
  the data-type owner;
- canonical arbitrary-precision signed integers and exact unsized Pigen literal
  identities, distinct from ordinary SystemVerilog unsized integers;
- one temporary analyzed-expression arena and one semantic materializer;
  analysis may intern canonical catalogue identities but never appends semantic
  expressions or lvalues;
- explicit semantic and constant conversion nodes, shared structural cast/type
  syntax, and SystemVerilog-style `type'(value)` casts;
- assignment-only destination conversion, exact final transfer type/shape
  invariants, and generated-width policy constraints;
- one canonical transfer-type descriptor catalogue owning source spelling,
  concrete/static classification, parameter form, write eligibility,
  valid/ready constants, consumption, production, ownership, and domain
  binding;
- generic transfer-argument identities on syntax and semantic signals, with
  descriptor-owned interpretation (`fifo` currently uses a depth expression);
- syntax-level base-type spellings which remain unclassified until the
  data-type owner resolves primitive spelling or semantic resolution finds a
  typedef;
- typed expression and recursive lvalue resolution;
- expression-use analysis with projections, contexts, and predicates;
- canonical conjunctive predicates and structural branch exclusion;
- simple clocked processes, direct guarded transfers, clock domains, signal
  incidences, self-feedback rejection, and pairwise ownership validation.

The focused source, preprocessing, syntax, semantic, predicate, expression,
use-analysis, and resolution tests pass. These modules are not linked into the
production executable.

The 2026-08-21 consistency cutover established one transfer-type enum and one
descriptor catalogue in the replacement middle, including the abstract input
type and the static constant laws. Syntax recognition, parameter parsing,
signal validation, assignability, incidence roles, ownership, and domain
binding now query that owner rather than carrying independent catalogues or
special-casing `fifo`. The production prototype's compact character catalogue
is explicitly named `pigen_prototype_transfer_descriptor`; it remains a
quarantined duplicate until production cutover. Generic AST and token variant
tags remain ordinary implementation discriminators.

The replacement middle now has one signal identity arena and one symbol
binding for statics and the other transfer types. Expression-use analysis and
direct-transfer incidence retain every participating signal. Canonical shape
identities and recognized declarator shapes are explicit as well.

The transfer-type owner maps each source transfer type to a backend-neutral
realization identity. Focused realization descriptors state capacity source,
ready dependency, occupancy, and reset behavior without naming SystemVerilog
primitives or generated interfaces. Reusing a realization requires only a new
transfer-type catalogue entry; a genuinely new realization additionally
extends the single future semantic-to-RTL adapter. General semantic passes do
not enumerate either catalogue.

The realization boundary is specified in
`docs/superpowers/specs/2026-08-24-transfer-realization-design.md`.

The intrinsic-expression boundary is specified in
`docs/superpowers/specs/2026-08-25-intrinsic-expression-semantics-design.md`.
Pigen's initial `int[n]` and `uint[n]` families are two-state integers with
lossless mixed-family promotion where a unique common representation exists.
`byte` is a two-state raw vector with no arithmetic interpretation. Operation
policy receives intrinsic operands only and returns effective operand types,
conversions, and result type. Ari completed exact literals, intrinsic sizing,
two-stage resolution, casts, assignment boundaries, and resource constraints
on 2026-08-25. Source-visible data-first declarations, lowering, and production
integration remain open.

As a rough architecture estimate, the replacement effort is about **25%**
complete overall: the reusable frontend and semantic foundation is around
**55%**, but authoritative production cutover is effectively **0%** and the
elastic RTL IR/backend does not yet exist. Treat this as a topology-of-work
estimate, not line-count progress.

## Remaining cutover boundary

The structured frontend still lacks complete target data-first declarations, generic
input specialization, the complete target data-first declaration grammar,
cases, atomic blocks, signal actions,
pipelines, FSMs, instances, and fabrics. Expression typing still lacks several
SystemVerilog contextual and aggregate forms. Preprocessing still lacks token
concatenation, stringification, and required advanced macro arguments.

The expression/type spine is ready for those declarations: casts and
declarations already share structural type syntax, and typedef-backed tests
exercise intrinsic arithmetic through complete semantic transfers without
adding a temporary second grammar. There is no elastic RTL IR or structured emitter. Production still lowers
fabrics, FSMs, atomic blocks, pipelines, declarations, and assignments through
rewritten source. In particular, pipeline placement still uses marker comments,
generated names, rescanning, and reparsing.

That pipeline rewrite also emits a private textual `ingress` declaration and
models it as a pseudo transfer type in the production primitive table. It is a
lowering endpoint, not one of Pigen's transfer types, and must not be added to
the common transfer-type enum. Remove it with the pipeline textual side channel;
the eventual structured pipeline lowering must represent stage ingress through
ordinary signal incidence and RTL-IR endpoints.

Do not link the partial model into production as an additional validator. A
structured slice becomes authoritative only when the corresponding textual
authority is deleted in the same change. `PLAN.md` owns the current cutover
order.

## Architectural prohibitions

- no reparsing compiler-generated source;
- no marker comments as internal placement channels;
- no semantic lookup by generated spelling or suffix;
- no declaration, dependency, type, or ownership discovery by substring scan;
- no textual type or guard equality;
- no byte-offset repair after rewriting;
- no feature-specific symbol, expression, or scope resolver;
- no emitter-side semantic inference;
- no shadow validation path, feature flag, compatibility implementation, or
  fallback for superseded Pigen behavior.

## Review gate

Before implementing a language or compiler feature, record:

1. the semantic invariant and owning object;
2. the resolution and type boundary;
3. the provenance path used by diagnostics;
4. every downstream consumer;
5. states made structurally unrepresentable;
6. behavioral and architectural verification;
7. confirmation that no pass rediscovers an already-known fact from text.

Passing tests are necessary but do not excuse a textual side channel. Tests for
superseded Pigen forms are replaced at clean breaks; ordinary SystemVerilog
compatibility tests are permanent.

This compact architecture record was organized by Ariadne after David's signal
and transfer-type decisions. Git history retains the discarded chronological
diary if a past implementation detail ever needs forensic recovery.
