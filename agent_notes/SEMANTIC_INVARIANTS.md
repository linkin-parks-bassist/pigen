# Semantic invariants for the replacement compiler middle

This document fixes the meaning that the new compiler architecture must
represent directly. `SPEC.md` is the current language authority and the test
suite is executable coverage of that language. Pigen is pre-release: a language
change updates the specification, implementation, examples, and tests together.
No prior Pigen form or implementation path has compatibility status. Ordinary
SystemVerilog compatibility remains the distinct contract stated in `SPEC.md`.

## Cross-cutting invariants

- Every source file has a stable `SourceId`. Every syntax and semantic node has
  a half-open source span referring to the original, immutable input.
- Every declaration, scope, type, expression, transport, clock domain,
  transfer, pipeline, stage, FSM, and fabric object has a stable typed identity.
  Array position may implement an identity, but spelling may not.
- Names are used only to introduce declarations and perform scope lookup.
  Resolved uses refer to declaration identities.
- Types are structural semantic objects. Source spelling and emitted spelling
  are provenance and presentation, never the test for type equality.
- A pass consumes structured objects from its predecessor. No pass communicates
  meaning through rewritten source, marker comments, generated suffixes, or a
  second textual scan.
- Unsupported syntax is either losslessly preserved outside Pigen's semantic
  boundary or rejected at its original span. Opaque syntax is never searched
  later for Pigen dependencies.

## Scopes and symbols

- Each declaration belongs to exactly one scope and introduces exactly one
  symbol identity.
- Each identifier expression is unresolved syntax or refers to exactly one
  symbol. There is no resolved-but-name-only state.
- Lookup order is encoded by scope parentage. Pipeline stages use
  `stage-local -> pipeline-local -> module-local` lookup.
- Duplicate declarations and shadowing are diagnosed while scopes are built,
  at the introducing declaration's span.
- Generated RTL names are allocated after semantic analysis and cannot affect
  lookup or identity.

## Types and expressions

- A resolved type records base kind, signedness, packed dimensions, and width
  expressions. Typedef identity and canonical resolved type remain separately
  available.
- Every resolved expression has an expression identity, type identity, source
  span, and structural operands.
- Identifier reads, lvalues, concatenations, casts, member selections, bit
  selections, indexed selections, calls, and conditional evaluation are
  distinct expression/use forms.
- Transport use analysis traverses expression nodes once and records base
  transport identity, projection, use context, and evaluation predicate.
  Repeated projections of one base transport are deduplicated by identity.
- Contextual sizing and signedness are established before transport or RTL
  lowering. The emitter never infers them from rendered text.

## Transports and transfers

- A transport descriptor owns kind-dependent validity, readiness, storage,
  consumption, and production behavior.
- An atomic transfer owns an ordered destination bit stream, one value bit
  stream, a guard predicate, a clock domain, and its source span.
- All consuming sources and buffered destinations in a transfer advance
  together or none do. Constants and degenerate values do not consume.
- A projected payload read consumes its complete base transport unless it is
  explicitly a `peek`.
- Concatenations and co-slices are width-partitioned views of one atomic bit
  stream, not collections of independent transfers.
- Buffered transports have one consumer. Multiple syntactic consumers are
  permitted only when their predicates are proven mutually exclusive.
- Producers of one buffered destination are likewise exclusive.
- A consuming buffered transfer cannot source itself, directly or through a
  grouped projection.

## Control and clock domains

- Every procedural semantic action carries its complete normalized guard and
  one clock-domain identity.
- Guard construction preserves SystemVerilog control nesting and dangling-else
  association. Mutual exclusion is a property of predicate structure, not
  string comparison.
- Stateful transport operations outside a supported clock domain, or crossing
  incompatible domains, are rejected at the action span.
- Reset, invalidate, validate, discard, and flush are explicit semantic
  actions; none is inferred by the emitter.

## Pipelines

- A pipeline is declared in one supported enclosing clocked process and is
  structurally inlined into its parent module.
- Pipeline fields are travelling packet state. Every stage reads an immutable
  incoming field set and jointly defines a mutable outgoing field set.
- Stage-local combinational declarations are not packet storage. A nonblocking
  write to a wire is structurally invalid.
- A stage advances atomically over its packet and inferred external transport
  inputs. Its complete enclosing procedural guard qualifies validity,
  readiness, and consumption.
- The first stage cannot read an uninitialised pipeline field. Later stages see
  preceding outgoing fields through stable field identities.
- `yield` is evaluated from the final outgoing field set and introduces one
  ordinary module-scope buffered transport with the pipeline's source name.
- Pipeline reset bindings attach predicates directly to generated valid state.
- Future packet-field pruning may remove only fields proven dead. Uncertain
  fields are preserved, and the decision remains auditable in RTL IR.

## FSMs

- An FSM owns one state type, one initial state, a unique set of state
  identities, and transitions whose targets resolve to those identities.
- State actions use the same expression, transport, guard, ownership, and clock
  services as ordinary transfers. An FSM does not privately rediscover them.
- Transition priority and transport atomicity are represented before RTL
  emission.

## Fabrics

- A fabric endpoint resolves to a module-instance port identity, never merely
  an `instance.port` string after resolution.
- Each output endpoint has exactly one connection. A direct destination has
  exactly one source; a routed destination may have several arbitrated sources.
- Endpoints remain source-blind and destination-blind. Route state is internal
  topology data and never public endpoint metadata.
- Topology, routes, reachability proofs, RTL, route manifests, and SVGs derive
  from one fabric model.
- Fabric payload compatibility is a type constraint at each connection, even
  while the current surface requires one `PAYLOAD_W`.

## Lowering boundary

- Semantic validation finishes before elastic RTL lowering begins.
- RTL IR contains only resolved ports, nets, registers, instances,
  combinational equations, and procedural updates with provenance.
- The SystemVerilog backend renders RTL IR and allocates collision-safe names.
  It performs no scope lookup, type inference, ownership analysis, or parsing.
- A structured replacement becomes authoritative only when the corresponding
  textual path is deleted in the same change. Dual paths and fallbacks are not
  representable repository states.

## Review gate for each replacement patch

Every patch must identify the invariant and owning node, resolution/type
boundary, provenance path, consumers, and newly unrepresentable invalid states.
It must also name the behavioral tests for the current design and structural
tests showing that no textual semantic side channel was introduced.
