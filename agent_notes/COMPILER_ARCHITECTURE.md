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

The current replacement implementation does not yet fully meet this criterion.
`include/pigen/data_type.h` and `src/data_type.c` now own the primitive
catalogue, canonical interning, builtin identities, alias unwrapping, packed
layout, projection, width, state domain, concatenation, sized-logic
construction, integral capability, and current unary, binary, and conditional
result rules. Constructor tags, raw interning, and the canonical record are
private to `src/data_type.c`; the public boundary exposes opaque
`pigen_data_type_id` identities and focused semantic queries. Semantic symbols,
expressions, constant expressions, lvalues, and signals name their data-type
field explicitly. The general semantic implementation, expression resolution,
and predicate construction no longer enumerate primitive constructors. Syntax
retains an optional written base token without classifying it. Resolution asks
the data-type subsystem to recognize primitive spelling, then considers typedef
lookup only when no primitive matches. Contextual conversion insertion, richer
numerical interpretation, and lowering still need consolidation. Do not extend
unrelated passes when adding `int`, `uint`, or `byte`.

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

## Current replacement foundation (2026-08-21)

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
- canonical constant-expression DAGs and symbolic width sums/products;
- a dedicated data-type interface and implementation owning canonical type
  construction, aliases, packed layout, projection, width, state domain,
  concatenation, and current operator-result typing;
- opaque `pigen_data_type_id` values outside that owner; constructor tags, raw
  interning, and concrete canonical records are not public compiler vocabulary;
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

The 2026-08-21 consistency cutover established one transfer-type enum in the
replacement middle, including the abstract input type and the static constant
laws, and removed the discarded vocabulary from current source, diagnostics,
tests, filenames, and documentation. The production prototype's compact
character representation now calls the axis `transfer_type`, but remains a
quarantined duplicate until production cutover; generic AST and token variant
tags remain ordinary implementation discriminators.

The replacement middle now has one signal identity arena and one symbol
binding for statics and the other transfer types. Expression-use analysis and
direct-transfer incidence retain every participating signal; the central
transfer-type descriptor supplies consumption, production, ownership, and
domain behavior. Canonical shape identities and recognized declarator shapes
are explicit as well; the full storage/lowering laws remain before this phase
is complete.

As a rough architecture estimate, the replacement effort is about **25%**
complete overall: the reusable frontend and semantic foundation is around
**55%**, but authoritative production cutover is effectively **0%** and the
elastic RTL IR/backend does not yet exist. Treat this as a topology-of-work
estimate, not line-count progress.

## Remaining cutover boundary

The structured frontend still lacks complete target declarations, abstract
input transfer types, the complete target data-first declaration grammar,
cases, atomic blocks, signal actions,
pipelines, FSMs, instances, and fabrics. Expression typing still lacks several
SystemVerilog contextual and aggregate forms. Preprocessing still lacks token
concatenation, stringification, and required advanced macro arguments.

There is no elastic RTL IR or structured emitter. Production still lowers
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
