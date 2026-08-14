# Compiler architecture: diagnosis and direction

## Candid assessment

Pigen's language and transport semantics are substantially better designed than
the compiler architecture currently implementing them.

The compiler works, and its verification suite exercises meaningful behavior.
It is not random code. Nevertheless, its large-scale structure is an accreted
source-to-source prototype whose original abstractions have been outgrown. It
has compiler-shaped components, but it does not yet have a compiler-shaped
spine.

The central warning sign is that strings are used not merely at the source and
output boundaries, but throughout semantic processing. Strings currently stand
in for identifiers, types, expressions, guards, scopes, packet layouts, and
inter-pass communication. Meaning is repeatedly recovered with token scans,
substring copies, textual comparisons, generated suffixes, source offsets, and
marker comments.

Consequently, the compiler often knows how something is spelled without having
a durable representation of what it means.

Passing tests do not make that architecture clean. The tests establish that a
considerable behavioral surface works; they do not establish that the intended
semantics follow naturally from the implementation. At present, much of the
correctness comes from coordinated local textual transformations. That makes
new features involving scope, types, control flow, or ownership disproportionately
expensive and fragile.

This is why the procedural pipeline change became an ordeal. Its semantics
require explicit representations of:

- lexical scopes and shadowing;
- resolved symbols and declaration identity;
- typed expressions and projections;
- read, write, consume, and produce uses;
- immutable incoming and mutable outgoing stage packets;
- external stage dependencies;
- transport validity, readiness, and ownership;
- enclosing guards, reset behavior, and clock domains.

The existing compiler does not represent those facts centrally. Several passes
therefore had to rediscover partial versions of them from source text and from
text emitted by earlier passes.

This is maintainable only while one person can remember all the implicit
textual conventions. It is not a sound base for sustained language growth.

## The architectural failure in one sentence

Generated or rewritten source text is acting as the compiler's intermediate
representation.

Strings are appropriate here:

```text
source text -> parser
backend -> emitted SystemVerilog text
```

They should not be the principal representation between those boundaries.

## What should be preserved

A rewrite should not discard the accumulated semantic work. Important assets
already exist:

- the language decisions in `SPEC.md` and `PLAN.md`;
- the ready/valid transport model and RTL primitives;
- the procedural-pipeline semantics;
- fabric topology, routing, and diagram algorithms;
- diagnostics whose behavioral requirements remain valid;
- the examples, especially the biquad bank;
- positive, negative, backpressure, atomicity, and full-throughput tests.

These tests are now an executable specification for a replacement compiler.
They are more valuable as rewrite acceptance criteria than the current lowering
code is as a foundation.

The lexer may also be reusable, provided it becomes the entrance to one
coherent syntax tree rather than a utility repeatedly invoked by independent
textual passes.

## Recommended large-scale structure

The compiler should have a single, explicit progression:

```text
source files
    |
    v
lossless syntax tree
    |
    v
scopes + symbol resolution + type resolution
    |
    v
typed Pigen semantic IR
    |
    v
transport/control graph and ownership validation
    |
    v
lowered elastic RTL IR
    |
    v
SystemVerilog emission
```

Each arrow should consume structured data and produce structured data. No pass
should communicate with another by editing source text and asking the next pass
to infer what happened.

### 1. Syntax frontend

Parse each compilation unit once into a real syntax tree. Preserve source spans
on every node for diagnostics. The syntax tree may retain trivia where useful,
but semantic work should never depend on reparsing a rendered form of it.

The frontend should represent at least:

- modules, ports, declarations, typedefs, parameters, and localparams;
- procedural blocks and control flow;
- expressions, lvalues, concatenations, slices, and casts;
- transfers and transport actions;
- pipelines, pipeline declarations, stages, and `yield`;
- FSMs and fabrics.

Unsupported SystemVerilog can be represented as opaque syntax only where it is
genuinely irrelevant to Pigen semantics. The boundary must be explicit; opaque
text must not later be searched for semantic dependencies.

### 2. Symbols, scopes, and types

Build scopes once. Every identifier use should resolve to a declaration identity,
not remain a name string.

Useful identity types would be morally equivalent to:

```c
typedef uint32_t SymbolId;
typedef uint32_t ScopeId;
typedef uint32_t TypeId;
typedef uint32_t ExprId;
typedef uint32_t TransportId;
typedef uint32_t PipelineId;
typedef uint32_t StageId;
```

The precise representation is unimportant; stable identity is essential.

Scope lookup should directly encode the intended precedence:

```text
stage-local -> pipeline-local -> module-local
```

Shadowing warnings then arise while constructing scopes rather than from
feature-specific textual scans.

Types should be structured. A type is not the string
`logic signed [W-1:0]`; it has a base kind, signedness, packed dimensions, and
width expressions. Typedef identity and canonical resolved type should both be
available. The SystemVerilog emitter may later choose how to spell it.

### 3. Typed expressions and use analysis

Expressions need nodes with resolved operands and source spans. The compiler
must be able to answer without scanning text:

- What type and packed width does this expression have?
- Is it signed?
- Which symbols does it read?
- Which transports does it consume?
- Is a reference a payload projection, an lvalue, an index, or a type query?
- Under what control predicate is it evaluated?
- Does it refer to incoming or outgoing pipeline state?

An expression should contain `SymbolId` references, not copied identifier
strings. Concatenation and slicing should be structural expression nodes.

This representation should become common infrastructure for ordinary
transfers, pipeline stages, FSM actions, and fabric endpoints. Each feature
should not implement a private approximation of expression analysis.

### 4. Semantic Pigen IR

After resolution, lower surface constructs into a typed semantic IR which is
still independent of exact emitted RTL.

A transfer might be represented conceptually as:

```text
Transfer {
    destinations: [ResolvedLvalue],
    value: ExprId,
    guard: ExprId,
    clock_domain: ClockDomainId,
    source_span: Span,
}
```

A pipeline stage might be represented as:

```text
Stage {
    scope: ScopeId,
    incoming_fields: [FieldId],
    outgoing_fields: [FieldId],
    external_inputs: [ResolvedInput],
    transforms: [FieldUpdate],
    guard: ExprId,
    clock_domain: ClockDomainId,
    source_span: Span,
}
```

Each `FieldUpdate` reads the immutable incoming packet. All updates jointly
define the outgoing packet. This makes stagewise nonblocking behavior a property
of the IR rather than an emitter trick.

`yield` should be a typed expression evaluated against the final outgoing stage
packet and should define one ordinary buffered transport in the enclosing
module. A later consumer refers to that transport by identity.

External stage inputs should be inferred from resolved expression uses. Their
validity and consumption behavior should derive from transport kinds in the
symbol table. There should be no separate scan of stage-body text.

### 5. Transport and ownership graph

Build a whole-unit graph after semantic lowering. Nodes represent transports,
stage boundaries, storage elements, and actions; edges represent production,
consumption, validity, and readiness dependencies.

This graph should be the authority for:

- atomic joins and co-slices;
- sole-consumer validation;
- mutually exclusive consumers;
- producer exclusivity;
- stage stall conditions;
- ready-path cycle detection;
- buffer/FIFO/skid placement;
- same-cycle consume-and-replenish behavior;
- reset and invalidation effects.

Guards should be expression nodes or normalized predicates, not rendered text.
Mutual exclusion should be proven or conservatively rejected at this level.

### 6. Lowered elastic RTL IR

Only after semantic validation should the compiler decide how to realize the
design in RTL. This IR should contain registers, combinational equations,
instances, ports, and always blocks, but no unresolved Pigen semantics.

Pipeline lowering is a settled language and backend constraint: every stage is
inlined structurally into its parent module. The RTL IR should make stage
packet boundaries, combinational transforms, elastic state, and parameter/type
dependencies explicit while preserving that parent-module realization. A
compiler-middle rewrite must reproduce the existing inlined behavior; it must
not reopen generated child modules as an emission alternative.

### 7. SystemVerilog backend

The backend should render the RTL IR. It should not discover symbols, infer
types, determine ownership, or reinterpret source expressions.

String construction belongs here. Generated-name allocation should be
centralized and collision-safe. No later compiler pass should parse the emitted
SystemVerilog.

## Architectural boundaries

The following dependencies should be one-way:

```text
lexer -> parser -> resolver -> semantic analysis -> RTL lowering -> emitter
```

Shared semantic services may include:

- source management and diagnostics;
- arenas and stable IDs;
- symbol tables and scopes;
- type interning and width expressions;
- expression traversal and use analysis;
- control predicates and clock domains;
- transport descriptors.

Feature modules—pipeline, transfer, FSM, and fabric—should consume those
services and produce common IR. They should not reach backward into source text
or sideways into another feature's generated spelling.

Important prohibitions:

- no reparsing compiler-generated source;
- no marker comments used for internal placement;
- no semantic lookup by generated suffix;
- no `strstr`-based declaration or dependency discovery;
- no textual type equality as semantic type equality;
- no byte-offset repair after source rewriting;
- no feature-specific duplicate symbol resolver;
- no emitter function performing semantic inference.

## Best practices for future implementation

### Make semantics structural

The correct behavior should follow from the representation. If stage reads are
supposed to observe the incoming packet, stage expressions must refer to
incoming `FieldId`s. Do not depend on carefully ordering textual substitutions.

### Preserve source provenance

Every semantic object should retain the span of the syntax which created it.
Generated IR should retain provenance chains where useful. Diagnostics should
never need to guess a source location from transformed text.

### Separate identity from spelling

Names are presentation and lookup keys. After resolution, declarations and
uses communicate by stable identity. Renaming a source symbol should not alter
the correctness of an unrelated analysis.

### Centralize invariants

There should be one authoritative implementation for each of:

- type resolution;
- scope lookup;
- expression-use discovery;
- transport-kind behavior;
- packed width and signedness;
- guard and clock-domain association;
- ownership validation.

Duplicated partial knowledge is the primary source of semantic drift.

### Prefer conservative rejection to accidental semantics

If an expression or SystemVerilog construct cannot yet be represented, report
that limitation at its source span. Do not preserve it as opaque text and then
make ownership or handshake decisions from an incomplete approximation.

### Treat tests as necessary, not sufficient

Each feature requires behavioral tests, negative diagnostics, stalls,
full-throughput cases, and interaction tests. It also requires an architectural
review: does the implementation express the semantic invariant directly, or
does it merely reproduce expected examples through textual coincidence?

### Make generated RTL auditable

Generated RTL should have a stable relationship to semantic IR objects. Names,
packets, stages, and handshake equations should be understandable without
reverse-engineering several string transformations.

## Rules for agent-driven compiler work

The failure was not merely insufficient coding effort. It was working at the
wrong level of abstraction: repeatedly optimizing for the next local test
instead of first determining what representation would make the semantics
natural.

Future agent instructions should include a gate such as:

> Before implementing a language feature, identify the semantic objects and
> invariants it requires. Show where each is represented in the compiler. If
> the current IR cannot represent them directly, stop and propose the necessary
> architectural change before writing the feature.

And a stronger prohibition:

> Do not recover semantic information from generated or rewritten source text.
> Strings are permitted at parsing and emission boundaries. Any textual
> rescanning, magic generated naming convention, marker comment, or reparsing
> of transformed source requires explicit approval.

Agents should also be asked to answer these questions before implementation:

1. What semantic invariant is being introduced or changed?
2. Which IR node owns that invariant?
3. Where are names resolved and types established?
4. How are source locations preserved?
5. Which existing passes consume the new information?
6. Does any pass need to rediscover information already known earlier?
7. What invalid programs become structurally unrepresentable?
8. What behavioral and architectural tests demonstrate correctness?

A patch which passes tests but introduces another textual side channel should
be rejected.

## Rewrite strategy

A roughly full rewrite of the compiler middle is warranted. A single enormous
replacement is unnecessary and risky; a strangler-style migration is possible
provided the new architecture is designed first and old textual mechanisms do
not leak into it.

Suggested order:

1. Freeze new language features temporarily and treat current tests as the
   compatibility suite.
2. Write down the semantic invariants for scopes, types, expressions,
   transports, transfers, pipelines, FSMs, and fabrics.
3. Introduce source management, arenas/stable IDs, syntax nodes, scopes,
   symbols, and structured types.
4. Parse ordinary modules and transport declarations into the new frontend.
5. Implement typed expression and lvalue resolution.
6. Lower ordinary transfers into the common semantic IR and match the existing
   transfer tests.
7. Implement the whole-unit transport/ownership graph.
8. Lower procedural pipelines into the same IR and match pipeline and biquad
   verification.
9. Move FSMs and fabrics onto the common frontend and semantic services while
   retaining their specialized algorithms.
10. Introduce a structured RTL IR and make SystemVerilog emission terminal.
11. Delete each old textual pass when its final consumer migrates. Do not leave
    fallback or compatibility paths.

During migration, compile selected test cases through both implementations and
compare simulation behavior rather than demanding textual RTL identity.

The new compiler should not import old implementation boundaries merely to make
migration convenient. In particular, do not create a new `pipeline` subsystem
which once again privately parses types, scopes, and expressions. Build the
shared frontend first.

## A useful initial experiment

Before committing to the full rewrite, implement one thin vertical slice:

```text
one module
  -> declarations and scopes
  -> one clocked block
  -> one atomic transfer
  -> typed semantic IR
  -> transport graph
  -> RTL IR
  -> SystemVerilog
```

Then reproduce the existing two-stage inlined elastic pipeline behavior with
one later-stage external buffer dependency. If this can be expressed without
source rewriting or semantic strings, the core architecture is probably
sound. The biquad bank then becomes the decisive integration target.

## Final perspective

The present implementation should be regarded as a successful semantic
prototype and an executable requirements generator, not as the desired mature
compiler architecture.

The language vision is not the problem. The problem is that semantic structure
has been encoded in coordinated text manipulation instead of represented as
data. Further local cleverness will deepen that debt. The next large move
should be to give the compiler a coherent semantic spine, after which pipelines
and transports can become straightforward consequences of the model rather
than recurring feats of textual reconstruction.
