# Signal model

Every runtime datum in Pigen is a **signal**:

```text
signal = data type × transfer type × declarator shape
```

The data type describes the carried bits and their interpretation. The
transfer type describes temporal validity, readiness, storage, consumption,
and production. The declarator shape is the ordered scalar/array extent after
the name. Packed dimensions belong to the data type; post-name dimensions
belong to shape. None of these facts is reconstructed from rendered text after
resolution.

The current Pigen primitive data types are `int[n]`, `uint[n]`, and `bit`.
Use `bit[8]` for neutral eight-bit storage. `byte` is not a Pigen primitive:
ordinary SystemVerilog `byte` remains its distinct signed eight-bit type and is
kept in the SystemVerilog spelling domain rather than mapped to `bit[8]`.

The concrete transfer types are `wire`, `reg`, `logic`, `buf`, `port`, `fifo`,
and `skid`. `wire`, `reg`, and `logic` are **static transfer types**, or
**statics** informally. `wire` is always valid and never ready as a procedural
destination; `reg` and `logic` are always valid and ready, and their reads do
not consume. Static laws are the constant cases of the same transfer algebra,
not exceptions to it.

Every Pigen module input exposes payload and valid into the module and ready
out. An unqualified input has an abstract transfer type; a written transfer
type constrains connection compatibility without changing the receiving body's
uniform consumption model. Constant static laws may later lower to ties, but
the signal and transfer type remain explicit semantically. Internal signals
and outputs follow the unqualified-transfer policy owned by their data type;
where that policy is unresolved or forbidden, the source must write a concrete
realization.

`net` retains its SystemVerilog realization meaning. It is not an umbrella noun
for Pigen signals. Likewise, a backend variable, queue, elastic slot, or
boundary interface is a realization of a signal, not its identity.

## Compiler laws

- There is one signal arena and one signal symbol binding for ordinary
  SystemVerilog and Pigen signals.
- Each resolved signal owns data type, concrete or abstract transfer type,
  canonical shape, direction, provenance, and a generic transfer argument.
- Written transfer occurrence and omission are distinct syntax states. An
  omitted occurrence is not a guessed transfer keyword.
- The transfer descriptor owns argument form and interpretation. `fifo[8]` is
  FIFO depth; it is never the payload width or a declarator dimension.
- The data-type owner alone decides what an omitted transfer occurrence means.
  Declaration syntax and resolution never enumerate primitive families.
- A transfer type may be concrete or abstract; it is never absent after
  resolution.
- Shape dimensions are colonless counts or explicit ranges. Scalar is the
  canonical empty shape. Indexing consumes leading unpacked shape before
  packed data; unsupported shaped slicing rejects rather than flattens.
- Expression-use and transfer-incidence analysis retain statics as signal
  identities. Transfer laws decide consumer, producer, ownership, and domain
  roles.
- Surface terminology is `signal`, `data type`, `transfer type`, `declarator
  shape`, and `static`. `kind` is not a language-level synonym for transfer
  type.

For example:

```systemverilog
int[16] fifo[8] pending[lanes];
```

has data type `int[16]`, transfer type `fifo` with descriptor-owned depth `8`,
and declarator shape `[lanes]`.

## Cutover boundary

Ari's 2026-08-27 shared-frontend cutover implements these declaration laws in
one structured syntax topology and one semantic resolver. It deletes Pigen
`byte`, retains ordinary SystemVerilog `byte` as reserved/opaque where
unsupported, and keeps abstract input policy in the data-type owner.

The structured frontend is not yet linked into `./pigen`. Production fixtures
remain on the prototype parser only until the narrow semantic-to-elastic-RTL
vertical slice supplies adapters, RTL IR, and emission. Do not create a bridge,
dual parser, or fallback in the meantime.

This model was fixed by David and organized by Ari; Rowan compacted the durable
record on 2026-08-28.
