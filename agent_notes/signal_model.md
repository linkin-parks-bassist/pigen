# Signal model

Every runtime datum in Pigen is a **signal**. A signal is the product of three
independent semantic components:

```text
signal = data type × transfer type × declarator shape
```

The data type describes the carried bits. The transfer type describes the
signal's temporal transfer law: validity, readiness, storage, consumption, and
production. The declarator shape describes scalar or array extent. None may be
recovered from source spelling after resolution.

The concrete transfer types are `wire`, `reg`, `logic`, `buf`, `port`, `fifo`,
and `skid`. `wire`, `reg`, and `logic` are **static transfer types**, or
**statics** informally. Their transfer-control laws are constant:

- `wire` is always valid and is never ready as a procedural destination;
- `reg` and `logic` are always valid and always ready, and reads do not consume.

Static behavior is not outside or beneath the transfer model. It is the
constant case of the same model, just as the empty set is still a set. The word
`dynamic` may denote the complement when a contrast is unavoidable, but
ordinary writing should simply say "transfer type."

Every module input is a signal with payload, valid, and ready at its boundary.
A unqualified input has an abstract transfer type constrained by this universal
interface. An explicit transfer type constrains the connection and makes an
incompatible connection erroneous; it does not give the receiving body a
different consumption model. An input does not need to know whether its peer
is a register, wire, buffer, or queue. It sees the same transfer interface and
can stimulate or stall the external dataflow through ready.

The meaning of a module may therefore depend parametrically on its connection
context without its body depending on the peer's realization. This is intended
to improve modularity and reuse. Verification must cover specialization at
module boundaries rather than weakening this abstraction.

A connected static may permit its constant validity or readiness law to be
folded into emitted `1'b0` or `1'b1` ties. That lowering optimization never
erases the signal or its transfer type from the semantic model. There is no
Pigen signal without formally defined valid and ready behavior.

`net` retains its SystemVerilog meaning: it is one possible emitted realization
of a signal. It is not Pigen's umbrella noun. Likewise, a backend register,
queue, or combinational connection is a realization decision, not the identity
of the source signal.

`byte` is an unsigned eight-bit bit-vector. It is not an integer type and has
no arithmetic signedness.

## Compiler consequences

- There is one signal identity space and one signal arena. Ordinary
  SystemVerilog nets and variables do not occupy a parallel semantic species.
- Each resolved signal owns its data type, transfer type, shape, direction,
  provenance, and generic transfer argument. The transfer-type descriptor owns
  the argument's meaning; `fifo` currently interprets it as a constant depth.
- A transfer type may be concrete or abstract; it is never absent.
- Expression-use analysis records signal identities for statics as well as
  stateful signals. Transfer incidence likewise includes all participating
  signals. Transfer-type laws decide which incidences consume, produce, bind a
  domain, require ownership, or lower to constants.
- Surface and internal terminology use `signal`, `transfer type`, and `static`.
  The discarded narrower umbrella term is removed rather than retained as an
  alias. `kind` is not a language-level synonym for transfer type; internal
  enum discriminators may still use a conventional tag field where unrelated
  to this vocabulary.

This note records decisions made by David and organized by Ariadne during the
architecture audit of 2026-08-21.

## Cutover record

On 2026-08-21, Ariadne merged the replacement compiler middle's former static
and general semantic arenas. `pigen_signal_id`, `pigen_semantic_signal`, and
`PIGEN_SYMBOL_SIGNAL` are now the sole runtime identities and bindings. This
includes signals declared in nested module-owned scopes: lexical ownership does
not create a second semantic species or prevent module ownership.

Every analyzed direct transfer now retains every participating signal and
separates syntactic read/write incidence from semantic consumer/producer roles.
The latter, clock-domain binding, and exclusivity checks are driven through the
central transfer-type descriptor. Statics therefore remain visible in the
incidence graph while their trivial laws avoid false consumption, production,
ownership, and domain constraints.

The unified signal and expression records now carry canonical `pigen_shape_id`
values. A shape owns an ordered list of dimensions; a dimension is either a
colonless count expression or an explicit left/right range. Structurally equal
lists intern to the same identity, while the full dimension records remain
available for diagnostics and resolution. Scalar is the canonical empty shape.
Packed dimensions remain in the data type; unpacked declarator dimensions form
the shape. This preserves locality through one direct ID while avoiding
duplicated dimension lists across signals and expressions.

Recognized declarators now parse their post-name brackets once into ordered
syntax-shape dimensions. Semantic resolution requires constant expressions,
interns the resulting shape, and attaches its ID to the signal. A symbol
expression inherits that same ID, and direct transfers reject unequal shape
IDs without rendering either shape back to text. Indexing consumes the leading
unpacked shape dimension while preserving the data type; packed indexing begins
only at scalar shape. Unpacked slices and concatenations currently reject
shaped operands rather than silently flattening them.

The canonical descriptor catalogue now owns spelling, classification, parameter
form, write eligibility, constant valid/ready laws, consumption, production,
ownership, and domain binding. Syntax recognition, resolution, signal
validation, assignability, incidence, and domain binding query it. The
descriptor still needs a precise storage/lowering representation; do not hide
the distinct `buf`, `port`, `fifo`, and `skid` realizations behind a vague
boolean merely to tick the plan item. That is the remaining part of phase 1,
not a reason to recreate parallel signal APIs.

After the one-arena, canonical-shape, structured declarator-shape, and canonical
transfer-descriptor cutovers, `make verify` completed successfully on
2026-08-21.
This proves the current
replacement-middle tests and production behavioral suite still pass; it does
not prove the architecture cutover complete. In particular, the complete
target data-first declaration grammar, storage/lowering laws, generic input
specialization, RTL IR, and production integration remain open.
