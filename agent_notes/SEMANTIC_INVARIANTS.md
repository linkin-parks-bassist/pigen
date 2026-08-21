# Semantic invariants for the replacement compiler middle

This document fixes the meaning that the new compiler architecture must
represent directly. `SPEC.md` is the target v1 language authority. The test
suite covers the production compiler and the settled behavioural semantics,
but prototype-syntax tests are replaced as clean-break cutovers land. Pigen is
pre-release: a language change updates the specification, implementation,
examples, and tests together. No prior Pigen form or implementation path has
compatibility status. Ordinary SystemVerilog compatibility remains the distinct
contract stated in `SPEC.md`.

## Cross-cutting invariants

- Every source file has a stable `SourceId`. Syntax provenance is a half-open
  expanded-token extent plus an origin chain and an optional contiguous
  physical-source span. Semantic nodes retain the strongest provenance their
  construct admits; cross-file or synthetic owners never fabricate a byte
  range.
- A fixed macro replacement token retains both invocation and definition-token
  origins. A substituted macro argument additionally retains the formal
  parameter and actual-token origins. Nested expansion must preserve these
  edges independently: primary diagnostics follow invocation edges, while
  spelling and explanatory diagnostics may follow actual/definition edges.
- Conditional compilation is resolved over the source-order macro environment
  before syntax construction. Inactive branches have no expanded tokens and no
  preprocessing side effects; nested selection state crosses textual include
  boundaries with the shared macro environment.
- Physical continuation markers in a multiline macro definition never become
  replacement tokens. The logical replacement sequence refers back to the
  original formal and replacement token origins, and the definition retains a
  physical span covering every continued line.
- An include operand expands into a private token destination and must resolve
  to exactly one quoted or angle-bracket path. Operand expansion tokens never
  enter the syntax token stream; the written include edge retains the physical
  directive and operand spans and the provider-selected `SourceId`.
- Every declaration, scope, type, expression, signal, clock domain,
  transfer, pipeline, stage, FSM, and fabric object has a stable typed identity.
  Array position may implement an identity, but spelling may not.
- Names are used only to introduce declarations and perform scope lookup.
  Resolved uses refer to declaration identities.
- Types are structural semantic objects. Source spelling and emitted spelling
  are provenance and presentation, never the test for type equality.
- A pass consumes structured objects from its predecessor. No pass communicates
  meaning through rewritten source, marker comments, generated suffixes, or a
  second textual scan.
- Each accepted source construct is parsed exactly once into syntax structure.
  Semantic resolution consumes that structure, lowering consumes semantic
  identities, and emission consumes RTL IR. No semantic or backend pass parses
  source fragments, rendered brackets, or compiler-generated text again.
- Unsupported syntax is either losslessly preserved outside Pigen's semantic
  boundary or rejected at its original span. Opaque syntax is never searched
  later for Pigen dependencies.

## Scopes and symbols

- Each declaration belongs to exactly one scope and introduces exactly one
  symbol identity.
- Every runtime module declaration denotes a signal, including ordinary
  SystemVerilog nets and variables. Each signal owns one identity, structural
  data type, concrete or abstract transfer type, declarator shape, direction,
  provenance, and any transfer-type parameters. Backend net, variable, and
  storage choices do not create a parallel semantic species.
- A symbol for a semantic object has one typed binding back to that object, and
  the object has the same symbol identity.  Recovering a `SignalId`,
  `ParameterId`, or `ModuleId` from a resolved name is constant-time and never
  scans an object arena or compares names again.
- Each identifier expression is unresolved syntax or refers to exactly one
  symbol. There is no resolved-but-name-only state.
- Lookup order is encoded by scope parentage. Pipeline stages use
  `stage-local -> pipeline-local -> module-local` lookup.
- Duplicate declarations and shadowing are diagnosed while scopes are built,
  at the introducing declaration's span.
- Generated RTL names are allocated after semantic analysis and cannot affect
  lookup or identity.

## Types and expressions

- A structured parameter has one symbol identity and one source expression
  occurrence for its value. Parameter declarations resolve in source order;
  later constant expressions refer to the parameter symbol rather than copying
  or substituting its initializer.
- A resolved type records its base type, signedness, packed dimensions, and width
  expressions. Typedef identity and canonical resolved type remain separately
  available.
- Every resolved expression has an expression identity, type identity, shape
  identity, provenance, and structural operands. A signal reference carries
  the referenced signal's canonical shape identity directly.
- Constant identity is an optional property of a resolved expression, not a
  precondition for its existence.  Parameter-only trees point at canonical
  constant DAG nodes; runtime signal reads remain fully typed
  semantic expressions with an invalid constant identity.
- Every explicitly sized based literal has an exact structural logic type and
  an exact-width four-state value.  Canonical literal identity is determined by
  type and normalized LSB-first `0`/`1`/`x`/`z` states, not source base or host
  integer capacity; each spelling occurrence retains its own expression
  identity and provenance.  Context-dependent unbased literals remain
  unresolved until their consumer establishes the required sizing semantics.
- Relational, equality, logical, and reduction expressions have an unsigned
  scalar-logic result type. Their operands remain typed structural expressions;
  constant analysis does not replace parameter references with copied values.
- Data-type capability and operator-result decisions have one semantic owner.
  Expression resolution and predicate construction query that owner and never
  enumerate primitive constructors. Typedef identity is transparent to
  capability checks while remaining available as the result identity when an
  operation preserves an operand's exact type.
- Canonical type construction, builtin identities, packed projection, width,
  state domain, and concatenation are owned by the same data-type subsystem.
  General semantic objects carry explicitly named `pigen_data_type_id` fields
  and use focused queries; they cannot inspect the private constructor tag,
  canonical record, raw interning operation, or primitive catalogue.
- Structured syntax stores the written base-type token, or absence for an
  implicit base. It does not classify primitive versus typedef. Primitive
  spelling is recognized only by the data-type subsystem during resolution;
  typedef lookup handles spellings that are not primitives.
- A resolved conditional expression owns condition, true-alternative, and
  false-alternative expression identities. Identically typed alternatives
  yield that type directly; mixed alternatives remain unresolved until their
  SystemVerilog width, signedness, and state-domain merge is represented.
- Identifier reads, lvalues, concatenations, casts, member selections, bit
  selections, indexed selections, calls, and conditional evaluation are
  distinct expression/use forms.
- Indexing an expression with non-scalar shape removes the leading unpacked
  dimension and preserves its data type. Only a scalar-shaped base may be
  indexed as a packed value. Selectors must themselves be scalar-shaped;
  unsupported unpacked slicing and concatenation are rejected instead of
  collapsing or reinterpreting shape.
- A concatenation expression owns an ordered child range.  Its value and
  constant identities preserve that order.  Its packed result type is unsigned;
  state domain is two-state exactly when every child is two-state, and its
  symbolic width is the canonical commutative sum of child packed widths.
- Packed-width queries recurse through typedefs and represent multidimensional
  width as a canonical commutative product.  Width algebra is structural and
  never folds symbolic bounds through a host integer.
- A resolved lvalue has its own stable occurrence identity and points at the
  expression which projects the destination and its type.  A projection lvalue
  owns one assignable base symbol and optional base signal; a concatenation
  lvalue owns an ordered, recursively nestable child range.  Every child must
  resolve before the concatenation exists. Direct signal symbols,
  transparent grouping, packed indexing, ordinary ranges, indexed part-selects,
  and concatenations are supported; parameters and operator trees are never
  accepted as destinations.  The expression owns the direct optional
  back-reference to its lvalue identity; no name or arena scan reconstructs the
  relationship.  Each packed projection owns separate base and selector
  expression identities.  Its base retains the complete projected expression
  identity; runtime subscripts use index context, while constant range and
  width expressions use type context.
- Signal use analysis traverses expression nodes once and records base
  signal identity, projection, use context, and evaluation predicate.
  Repeated projections of one base signal are deduplicated by identity.
- Use analysis retains every symbol occurrence separately, including its
  projected `ExprId` and predicate, while a parallel signal summary contains
  each `SignalId` once.  Ternary conditions are read under the incoming
  predicate; true and false alternatives are traversed under conjunctions with
  opposite polarities of that same condition identity.  An impossible path
  contributes no uses.
- Lvalue use analysis records the projected lvalue expression under the
  lvalue context, each runtime subscript under index context, and selector
  bounds which determine type under type context.  Concatenated lvalues walk
  their child identities in source order and record each projection
  independently.  If a signal is both read and written in an analyzed set,
  one deduplicated signal summary carries all applicable context bits while
  the occurrence records remain distinct.
- Contextual sizing and signedness are established before transfer or RTL
  lowering. The emitter never infers them from rendered text.
- Builtin semantic types have model-owned stable identities. Constant-expression
  checking is a policy on the shared typed-expression resolver used by
  parameters, dimensions, depths, and future semantic consumers; declaration
  features do not own private operator maps or result-typing rules.

## Signals and transfers

- Data type, transfer type, and declarator shape are independent semantic
  identities. A rendered SystemVerilog declaration never stands in for that
  product.
- A parsed declarator owns an ordered syntax-shape list whose dimensions are
  either colonless counts or explicit ranges. Resolution requires constant
  dimension expressions and interns the list as one canonical `ShapeId`.
- Every signal has a transfer type. The concrete transfer types are `wire`,
  `reg`, `logic`, `buf`, `port`, `fifo`, and `skid`; an unqualified input has
  an abstract transfer type constrained by its connection. `wire`, `reg`, and
  `logic` are statics whose transfer-control laws are constant members of the
  same algebra.
- A module input is a boundary endpoint with one uniform payload/valid/ready
  contract. Its optional written transfer type constrains compatible
  connections; it never changes consumption semantics inside the receiving
  module. Static laws may lower to constant ties without disappearing from the
  semantic model.
- A transfer-type descriptor owns transfer-type-specific validity, readiness,
  storage, consumption, production, ownership, domain, and lowering behavior.
- An atomic transfer owns an ordered destination bit stream, one value bit
  stream, a guard predicate, a clock domain, and its source span.
- A direct whole-expression transfer requires identical source and destination
  shape identities. A mismatch is diagnosed from semantic objects; bracket
  text is never rendered and compared.
- Each transfer also owns one deduplicated incidence entry per participating
  signal.  The entry records producer and consumer roles independently;
  destination-index reads are consumers even though they occur syntactically
  on the left-hand side.
- All consuming sources and buffered destinations in a transfer advance
  together or none do. Constants and statics do not consume.
- A projected payload read consumes its complete base signal unless it is
  explicitly a `peek`.
- Concatenations and co-slices are width-partitioned views of one atomic bit
  stream, not collections of independent transfers.
- Buffered signals have one consumer. Multiple syntactic consumers are
  permitted only when their predicates are proven mutually exclusive.
- Producers of one buffered destination are likewise exclusive.
- A consuming buffered transfer cannot source itself, directly or through a
  grouped projection.

## Control and clock domains

- Evaluation predicates are canonical conjunctions of `(ExprId, polarity)`
  atoms, with explicit interned true and false identities.  Atom order does not
  affect identity; repeating an atom is idempotent; adding the opposite polarity
  yields false.  Two guards are proven mutually exclusive when they require
  opposite polarities of the same expression identity (or either is false).
- Predicate atoms refer to already typed integral semantic expressions.  They
  never contain copied source text, reconstructed identifiers, or backend RTL.
- Every procedural semantic action carries its complete normalized guard and
  one clock-domain identity.
- A clock domain is canonical by resolved clock declaration identity and edge,
  while each clocked process retains its own syntax identity, clock-expression
  occurrence, module ownership, and provenance.  Repeated processes on the
  same edge therefore share a domain without collapsing their source uses.
- Guard construction preserves SystemVerilog control nesting and dangling-else
  association. Mutual exclusion is a property of predicate structure, not
  string comparison.
- Stateful signal operations outside a supported clock domain, or crossing
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
- A stage advances atomically over its packet and inferred external signal
  inputs. Its complete enclosing procedural guard qualifies validity,
  readiness, and consumption.
- The first stage cannot read an uninitialised pipeline field. Later stages see
  preceding outgoing fields through stable field identities.
- `yield` is evaluated from the final outgoing field set and introduces one
  ordinary module-scope buffered signal with the pipeline's source name.
- Pipeline reset bindings attach predicates directly to generated valid state.
- Future packet-field pruning may remove only fields proven dead. Uncertain
  fields are preserved, and the decision remains auditable in RTL IR.

## FSMs

- An FSM owns one state type, one initial state, a unique set of state
  identities, and transitions whose targets resolve to those identities.
- State actions use the same expression, signal, guard, ownership, and clock
  services as ordinary transfers. An FSM does not privately rediscover them.
- Transition priority and transfer atomicity are represented before RTL
  emission.

## Fabrics

- A fabric belongs to one parent module and connects ports of child instances
  owned by that module. It is never a separate public semantic design unit.
- A fabric endpoint resolves to a module-instance port identity, never merely
  an `instance.port` string after resolution.
- Each output endpoint has exactly one connection. A direct destination has
  exactly one source; a routed destination may have several arbitrated sources.
- Endpoints remain source-blind and destination-blind. Route state is internal
  topology data and never public endpoint metadata.
- Topology, routes, reachability proofs, RTL, route manifests, and SVGs derive
  from one fabric model.
- Fabric payload compatibility and internal link width are derived from the
  resolved endpoint types at each connection; there is no fabric-wide payload
  type parameter in the target language.

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
