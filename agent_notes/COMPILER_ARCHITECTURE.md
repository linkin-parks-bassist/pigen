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

## Replacement-spine status (2026-08-17)

The first replacement foundations now exist in `source.c`, `syntax.c`,
`semantic.c`, `expression_resolve.c`, and `resolve.c`: immutable source storage, typed
IDs, source spans, a partial syntax tree, scopes, symbols, structural packed
types, and resolved module/typedef/internal-transport declarations.  These
files are not linked into the production compiler.  The live compiler still operates through
the textual lowering path in `pigen.c`, `assignments.c`, and `pipeline.c`.

Each immutable source file owns an ordered table of line-start byte offsets,
including the first line and the empty line after a trailing newline.  Source
location lookup is a binary search in that table rather than a scan from the
beginning of the file.  Columns remain byte-based, matching lexer spans;
changing the diagnostic column policy is a separate concern and must not alter
the byte-offset provenance model.

The frontend now has two deliberately distinct views.  The written-source view
owns one raw tokenization per physical `SourceId`, refers back to the source
manager's exact immutable bytes, and records written include sites and their
resolved file edges.  The expanded view owns the token order seen by semantic
parsing.  Its hierarchical syntax tree makes structured declarations replace
their expanded token regions, makes declarators children of declarations, and
keeps unparsed SystemVerilog explicit as opaque syntax.  Only the written view
is lossless with respect to the physical input files.  The following early
corrections are complete:

- Distinct integer-literal occurrences now receive distinct semantic
  expression IDs and retain their own provenance.
- A declaration such as `buf [7:0] a, b;` is one transport-declaration node
  owning two declarator nodes; declaration identity no longer overlaps.
- ANSI transport ports, including inherited comma-list declarators, use the
  same declaration representation as internal transports.

Canonical constant expressions are a separate arena, identified by
`pigen_const_expr_id`.  A source expression occurrence has an `ExprId`, type,
and source span, and refers to a canonical constant expression when it is
constant.  Interned `TypeId` dimensions refer only to canonical constant
expressions.  Thus equal widths compare cheaply without collapsing distinct
source occurrences or borrowing one occurrence's provenance.  The canonical
arena represents integers, exact sized bit vectors, resolved parameter symbols,
and typed unary, binary, and conditional operator DAG nodes.  Sized bit vectors
are normalized to LSB-first `0`/`1`/`x`/`z` states, so equivalent base spellings
share canonical identity without losing the separate source occurrences which
produced them.  Their width is not limited by a host integer.  Packed bounds and
FIFO depths now parse into a shared expression-syntax arena and resolution
constructs distinct semantic occurrences backed by those canonical nodes.

`expression.c` is the shared structural expression parser for every future
Pigen semantic context.  Its arena represents literals, names, grouping, unary
and binary precedence, conditionals, concatenation and replication, calls,
member and scope access, indexing, ordinary and indexed selects, and casts.
Operators are semantic enum values rather than spellings.  Child-reference
ranges keep variable-arity calls and concatenations compact without imposing
tree-specific allocations.  The lexer recognizes SystemVerilog's
multi-character operators by longest match; the live fabric parser was adjusted
to count the spelling of repeated hyphens in its contextual arrows.

The language boundary is settled: expressions inside Pigen semantic constructs
remain ordinary SystemVerilog expressions, not a smaller Pigen-only language.
An expression form not yet represented is rejected explicitly at that boundary;
ordinary SystemVerilog outside it remains opaque and unchanged.  Streaming
concatenations, assignment patterns, and the less common primary forms are not
yet represented.  Do not handle them with textual scanning.

Preprocessing is a token stage before syntax, not a symbolic-macro exception in
the expression model.  `preprocess.c` now provides stable macro and token-origin
identities, retains replacement tokens and formal parameters, and recursively
expands object-like and function-like macros.  A fixed replacement token records
its invocation and definition-token origins.  A substituted argument token
instead records its invocation, formal-parameter, and actual-token origins.
Consequently nested expansion preserves three distinct questions: which user
call caused a token to appear, which actual spelling supplied it, and through
which formal binding it entered the replacement.  Macro arguments are split
structurally across nested parentheses, brackets, and braces before recursive
expansion; commas inside those groups do not become argument separators.  A
continued `define` is first projected into one logical sequence of raw-token
indices.  Continuation backslashes are absent from that sequence, while formal
parameters and replacement tokens retain their exact physical source origins;
the macro definition span still covers all participating physical lines.
`ifdef`/`ifndef`/`elsif`/`else`/`endif` use one nested selection stack and the
source-order macro environment.  Inactive branches are inert: they neither emit
tokens nor execute definitions, undefinitions, includes, or macro calls.  The
conditional directive ends after its required keyword/name tokens rather than
owning the physical line, so selected source may follow it on that same line.
The conditional stack and macro environment cross include boundaries just as
the textually included token stream does.  Quoted includes are loaded recursively
through a caller-owned source provider.  The provider, not the preprocessor,
owns path resolution and I/O; it returns a `SourceId` already registered with
the shared source manager.  Included files retain their own source identity and
are checked for recursive inclusion.  Include operands expand through a private
token destination rather than the program-token arena.  The result must be
exactly one quoted path or one angle-bracket path; the written include edge
retains the physical operand span even when its spelling came from a macro.
Token concatenation/stringification and variadic/default macro arguments remain
explicit unfinished preprocessor work.

Structured syntax now consumes only the immutable preprocessed token stream;
the raw-token parser entry point has been removed.  Every syntax location owns
a typed half-open token extent, a diagnostic origin, and an optional contiguous
original-source span.  Names are token identities.  Resolution compares the
spelling reached through token provenance, while errors use the expansion
location, so macro-generated spelling and user-facing diagnostics are no longer
conflated.  A macro-expanded bound or FIFO depth is parsed from expanded tokens
and retains the invocation as its expression provenance.  Never
manufacture a contiguous source span when future includes make one unavailable.

The include losslessness boundary is settled: do not attach written include
directives to the expanded syntax tree.  `pigen_preprocess_result` contains a
`written` source view and a separate `expanded` token view.  The syntax parser
accepts only the latter, making the dependency unambiguous in the type system.
The written view preserves the former include directive and edge; the expanded
view contains the included tokens in compilation order.  Cross-file syntax
locations retain token extents and provenance but no fabricated byte span.
Opaque expanded syntax likewise covers token extents only; physical gaps,
comments, directive text, and other exact bytes belong exclusively to the
written view.  A compilation-unit semantic scope may consequently have no
single source span.  Such a scope uses the invalid-span sentinel deliberately;
individual declarations and diagnostic sites retain concrete provenance.

The shared lexer also previously classified decimal tokens as identifiers
because its general identifier-character branch preceded its digit branch.
Numbers now have their actual token kind.  This matters to structured constant
resolution and also removes accidental name treatment from the prototype
pipeline lexer users.

The following limitations therefore still prevent cutover:

- Constant resolution handles unsized decimal literals, exact explicitly sized
  `b`/`o`/`d`/`h` literals, parameter-symbol leaves, integer
  arithmetic/bitwise trees, and boolean comparison, equality, logical, and
  reduction trees.  Binary, octal, and hexadecimal values preserve four-state
  digits; numeric decimal values are converted directly into the requested
  width without a host-integer intermediate.  Literal signedness and width own
  a structural logic type.  Boolean-producing operators share one interned
  unsigned scalar-logic result type.  Conditional constant expressions are
  structured when both branches already have the same resolved type; mixed
  branch types remain unresolved until SystemVerilog's conditional merge rules
  are represented.  Unbased, unsized based, decimal `x`/`z`, and aggregate
  forms remain syntax only until their context and SystemVerilog result typing
  are represented; assigning them a generic integer type would silently encode
  false sizing semantics.  Untyped integer
  `parameter` and `localparam` declarations are
  now structured in module parameter lists and module bodies.  Typed and
  aggregate parameter forms remain opaque until their types can be represented
  without approximation.
- Transfers, pipeline stages, and other procedural constructs do not yet point
  at the shared expression arena.  Until one of those paths becomes authoritative
  and its textual scanner is deleted, the production compiler remains textual.

Do not integrate the partial model into the production driver merely as an
additional validation pass; that would create a second authority without
deleting a textual path.

Parameter declarations are resolved in source order into dedicated semantic
parameter objects.  Each object owns its syntax identity, module, symbol,
constant value expression, locality, and provenance.  Parameter-derived packed
bounds and FIFO depths retain canonical symbol-expression nodes rather than
copying or evaluating the parameter's source spelling.  An initializer may
refer to an earlier parameter in the same module; it cannot acquire its own
symbol before its value has resolved.  Unsupported typed parameter declarations
remain ordinary opaque SystemVerilog and do not create partial semantic facts.

Relational, equality, logical, and reduction operators map from syntax operator
enums to semantic operator enums without rendering their spelling.  Their
result is a canonical unsigned scalar-logic type, while source-expression
occurrences retain their own provenance and point at interned constant DAG
nodes.  This is deliberately structural rather than an evaluator: elaboration
values are not folded into host integers, and parameter references remain
symbol leaves.

Conditional expressions likewise have distinct occurrence and canonical DAG
nodes.  Their condition and both alternatives are expression identities, not
source fragments.  The current resolver accepts only integral conditions and
identically typed alternatives, making the result type exactly the shared
branch type.  Do not broaden this by choosing one branch or a generic integer;
mixed widths and signedness require the proper SystemVerilog merge operation.

`expression_resolve.c` owns the shared recursive resolution of expression
syntax into typed semantic occurrences.  Constant-expression checking is a
policy of this service, not a separate expression species: parameter-only trees
also point at canonical constant DAG nodes, while runtime value and transport
reads carry an invalid constant identity without losing their type, provenance,
or structural operands.  Declaration resolution uses the constant policy for
parameter values, packed bounds, and FIFO depths; future transfers and pipeline
stages must use the general policy rather than adding a feature-local resolver.
Semantic unary and binary operator enums are correspondingly shared and do not
carry a misleading `const` prefix.  The semantic model also owns lazy stable
identities for the ordinary unsized-integer type and the unsigned scalar-logic
boolean result type, so callers do not privately intern competing builtin
types.

Symbols and their current semantic objects are bidirectionally linked.  Module,
parameter, and transport arenas are grown only by semantic-model constructors;
each constructor validates scope ownership, type agreement, required constant
expressions, and provenance before installing the object's typed identity in
its symbol.  Typed symbol lookup functions verify the reverse link.  Expression
use analysis must use this binding to recover a `TransportId` from a symbol—it
must not scan the transport arena or compare declaration names a second time.

`predicate.c` owns canonical evaluation guards independently of rendered
expressions.  A predicate is an interned conjunction of typed expression
identities and required truth polarities; empty conjunction and contradiction
have explicit true and false identities.  Conjunction insertion sorts by
`ExprId`, removes repetition, and turns opposite requirements into false.
Mutual-exclusion queries merge the sorted atom lists and therefore prove
opposite conditional branches without comparing strings.  This deliberately
small algebra represents nested `if`/`else` and ternary evaluation paths; a
future need for disjunction must extend the predicate algebra rather than
encode guards as rendered text.

`expression_use.c` performs the first shared semantic-expression traversal.
Its read analysis records every symbol occurrence with its projected
expression identity, typed symbol identity, evaluation `PredicateId`, and
`TransportId` when the symbol denotes a transport.  A separate summary array
deduplicates transports by identity while occurrence records preserve repeated
projections and provenance.  Conditional expressions read their condition
under the incoming predicate and traverse their alternatives under opposite
condition polarities; false predicates prune unreachable uses.  The data model
reserves lvalue, index, and type contexts, but those entry modes remain rejected
until their legal semantic expression forms are represented—do not approximate
them with read traversal.

The semantic model now also interns resolved lvalue occurrences by their
projecting `ExprId`.  A lvalue owns its type, assignable base `SymbolId`, optional
base `TransportId`, and provenance.  Direct ordinary values, direct transports,
and transparent grouping are authoritative; parameters and operator trees are
rejected.  Lvalue use analysis feeds the same occurrence and deduplicated
transport arrays as read analysis, setting a distinct context bit.  Member,
index, select, and concatenated destinations must extend this structural lvalue
model before procedural transfer syntax claims them.

## Rewrite strategy

A roughly full rewrite of the compiler middle is warranted. It may be built in
small, independently tested slices, but there is no compatibility architecture:
when a slice becomes authoritative, it replaces and deletes the corresponding
textual mechanism in the same change. No dual path, feature flag, deprecated
form, or fallback remains in the compiler.

Suggested order:

1. Freeze new language features temporarily and treat current tests as an
   executable description of current semantics. Deliberate language changes
   update those tests rather than preserving prior Pigen behavior.
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
11. Delete each textual pass in the same change that makes its structured
    replacement authoritative.

The new compiler should not import current implementation boundaries merely to
stage the rewrite conveniently. In particular, do not create a new `pipeline` subsystem
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
