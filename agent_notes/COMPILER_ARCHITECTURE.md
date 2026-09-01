# Compiler architecture

## Spine

The production compiler is a successful semantic prototype, not the intended
architecture. It still recovers meaning from rewritten source, token scans,
generated names, byte offsets, and marker comments. Strings belong only at the
two terminal boundaries:

```text
source text -> frontend
backend -> emitted SystemVerilog
```

The replacement compiler has one structured progression:

```text
immutable sources and preprocessing
    -> written and expanded token views
    -> structured syntax
    -> scopes, symbols, data types, transfer types, and shapes
    -> typed expressions, lvalues, predicates, and clock domains
    -> semantic signals, transfers, pipelines, FSMs, and fabrics
    -> incidence, ownership, and ready-dependency graphs
    -> elastic RTL IR
    -> SystemVerilog and fabric SVG emission
```

Every arrow consumes identities and records from its predecessor. Unsupported
SystemVerilog remains lossless opaque syntax outside Pigen's semantic boundary;
no later pass searches opaque text for meaning.

## Ownership laws

Every runtime datum is one signal:

```text
signal = data type × transfer type × declarator shape
```

Ordinary SystemVerilog nets and variables are signals too. Backend nets,
variables, queues, and interfaces are realizations, not a second semantic
species. `net` retains its precise SystemVerilog meaning.

Each varying catalogue has one owner and narrow queries:

- the source layer owns immutable text, line tables, spans, preprocessing, and
  written/expanded provenance;
- syntax owns written structure, including the distinction between an omitted
  transfer occurrence and a written one;
- the data-type subsystem owns primitive spelling, canonical representation,
  packed layout, signedness, numerical interpretation, conversion, operation
  resolution, and unqualified-transfer policy;
- the transfer-type subsystem owns spelling, parameter form, write
  eligibility, valid/ready laws, incidence roles, ownership, domain binding,
  and backend-neutral realization identity;
- expression resolution owns typed operation and conversion decisions;
- semantic analysis owns signals, transfers, guards, domains, and validation;
- elastic RTL lowering owns ports, nets, registers, instances, equations, and
  procedural updates; and
- emitters render RTL IR or resolved fabric topology without rediscovering
  semantics.

General declaration resolution never enumerates primitive data-type families.
It resolves the data type first, asks that owner what omission means, and asks
the written transfer descriptor to interpret its argument. Thus FIFO depth is
descriptor-owned structural data and never a payload dimension or declarator
shape. Adding or deleting a primitive should normally touch only the data-type
owner, boundary spellings, specification, and focused tests.

This is compile-time modularity, not a runtime registry or plugin framework.
Small tables, tagged records, and ordinary private functions are preferable to
speculative generality.

## Expression boundary

Expression meaning is intrinsic. Resolution receives no expected-result type;
the data-type owner returns effective operands, conversions, operation
identity, and result type. A typed assignment resolves its destination
separately and asks the destination family for one final conversion above the
complete RHS. Every non-identity conversion is explicit and identity
conversions disappear.

Pigen decimals and Pigen type/transfer counts use exact integers. Ordinary
SystemVerilog parameters and structural ranges retain the SystemVerilog literal
domain. Exact width algebra remains structural; policy may reject excessive
known generated widths or carry a symbolic width constraint, but it never
clamps semantic results. Details which lowering needs are stored once in
operation and conversion records rather than re-inferred by later passes.

The current Pigen primitive data types are `int[n]`, `uint[n]`, and `bit`.
Neutral eight-bit storage is `bit[8]`. Ordinary SystemVerilog `byte` is reserved
to the SystemVerilog spelling domain and is not constructed as a Pigen data
type or silently mapped to `bit[8]`.

## Current structured foundation

Ari recorded the 2026-08-27 implementation boundary: the shared, unlinked
frontend has immutable provenance, preprocessing, one declaration syntax
topology, scopes and symbols, structural data types and shapes, exact constant
expressions, typed expressions and lvalues, predicates, clock domains, one
signal arena, transfer incidence, and owner-driven declaration resolution.
Source-visible data-first declarations preserve written transfer occurrence or
omission, abstract Pigen inputs, descriptor-owned FIFO depth, and post-name
declarator shape independently. The resolver contains no primitive-family
declaration switch.

Supported ordinary SystemVerilog declarations use the same topology. Ada
tightened the declaration boundary after final compatibility review: syntax
commits only a written post-type transfer, a colonless Pigen count, the narrow
ordinary-static adapter, or a previously accepted structured typedef name.
The typedef-name query is source ordered and lexically shadowing. Ada's
residual pass made unsupported typedefs roll back every syntax arena and, only
when their transaction structurally identifies the introduced name, record an
opaque shadow barrier rather than an eligible type. Leading transfer spellings
reject only when a following eligible data type and declarator make the deleted
transfer-first form unmistakable, so ordinary `buf` gates and ambiguous port
names remain lossless. Malformed decisive Pigen syntax still diagnoses at its
owning source span rather than falling back to opacity.

The structured modules are not linked into `./pigen`. Production fixtures must
therefore remain on the quarantined prototype parser until the first vertical
lowering slice becomes authoritative. The production compact transfer table is
explicitly named `pigen_prototype_transfer_descriptor`; it is a temporary
quarantined duplicate, not a second shared catalogue. Do not migrate its
fixtures to the structured parser piecemeal.

The prototype pipeline rewrite also emits a private textual `ingress` and
models it in the production table. `ingress` is a lowering endpoint, not a
Pigen transfer type: never add it to the common transfer catalogue. Keep it
quarantined until structured pipeline lowering replaces that textual side
channel with ordinary signal incidence and RTL-IR endpoints.

## Next cutover

The next step is a narrow semantic-to-elastic-RTL vertical slice: one resolved
module, its declarations, one clocked process, and direct atomic transfers
through explicit adapters, elastic RTL IR, and terminal SystemVerilog emission.
RTL IR, adapters, production integration, pipelines, FSMs, fabrics, and their
structured emission remain pending.

Ada resumed this cutover on 2026-09-01. David confirmed that the existing
specification, plan, and notes remain the approved architecture and that work
should proceed from this boundary through completion of the overhaul. The
vertical-slice design fixes an ordered compilation-output model: opaque
ordinary SystemVerilog spans remain outside elastic RTL IR and are copied only
at the terminal boundary, while structured output items refer to RTL module
identities. See
`docs/superpowers/specs/2026-09-01-elastic-rtl-vertical-slice-design.md`.

Never link the partial frontend as an additional validator. A structured slice
becomes authoritative only when its corresponding textual parser, scanner, and
emitter are deleted in the same change. Historical approved documents in
`docs/superpowers/` remain evidence of the decisions in force when written;
current status belongs here and in `PLAN.md`.

## Prohibitions and review gate

- no reparsing compiler-generated source;
- no marker comments as placement channels;
- no semantic lookup by generated spelling or suffix;
- no dependency, type, shape, or ownership discovery by substring scan;
- no textual type, shape, or guard equality;
- no feature-private scope, symbol, expression, or primitive resolver;
- no emitter-side semantic inference; and
- no shadow validator, fallback, compatibility path, or second Pigen dialect.

Before a replacement patch, identify the owning invariant, structured input and
output, provenance path, downstream consumers, and states made unrepresentable.
Verify current behavior and the absence of a textual semantic side channel.

Rowan compacted this note during the 2026-08-28 documentation cutover; Ada
recorded the final declaration-ownership compatibility boundary on 2026-08-30.
