# Source-Visible Data-First Declarations Design

**Date:** 2026-08-27

**Status:** Approved for implementation

## Purpose

Make the target data-first declaration language authoritative in Pigen's
shared structured frontend. Replace the remaining transfer-first and
static-versus-nonstatic syntax split with one declaration representation which
preserves the independent data type, transfer type, and declarator shape of
every recognized signal.

This is a shared-frontend cutover. It does not connect the structured middle to
the production executable. Production integration begins with the separately
planned vertical lowering slice; this work must not introduce an adapter back
into the textual prototype, a shadow validator, or a second emitter.

## Fixed language decisions

Declarations place the data type first, followed by the transfer type and then
the declarator:

```systemverilog
int[16] buf sample;
uint[24] skid response;
bit port finished;
bit[8] logic tag;
packet_t fifo[DEPTH] queue[LANES];
```

`fifo[DEPTH]` is one parameterized transfer-type occurrence. Brackets after
`queue` belong only to the declarator shape. The full decomposition of the last
example is therefore:

```text
data type      transfer type      declarator + shape
packet_t       fifo[DEPTH]        queue[LANES]
```

Pigen's source-level data types in this slice are `int[n]`, `uint[n]`, and
`bit`. Neutral eight-bit data is spelled `bit[8]`.

Pigen no longer defines `byte`. The Pigen `byte` constructor, canonical
identity, capability and operation rules, public APIs, examples, and tests are
deleted outright. Ordinary SystemVerilog `byte` retains its SystemVerilog
meaning and is not claimed as Pigen syntax. Where the shared frontend does not
yet model an ordinary `byte` declaration semantically, that declaration remains
opaque and lossless.

Bare `bit` overlaps the SystemVerilog spelling but has the same two-state
unsigned value representation. In a `.pigen` module, an unqualified
`input bit flag;` is an abstract Pigen input. Unqualified internal and output
`bit` declarations retain supported ordinary-SystemVerilog static inference.
This input rule is an explicit compatibility exception: future lowering must
specialize static connections to constant valid/ready laws and decide the
top-level adapter policy as part of the vertical slice.

## Scope

This slice:

- accepts target data-first internal and ANSI-port declarations in the shared
  syntax parser;
- accepts `fifo[depth]` as the sole parameterized transfer spelling;
- gives every recognized declaration one syntax topology;
- resolves explicit transfers, abstract inputs, static defaults, transfer
  arguments, data types, and declarator shapes into the unified semantic signal
  arena;
- deletes transfer-first Pigen fixtures and both static-specific syntax and
  resolution paths;
- deletes Pigen `byte` throughout the structured frontend and its public
  language documentation;
- preserves supported ordinary SystemVerilog declarations and leaves other
  ordinary SystemVerilog opaque;
- updates the specification, public introduction, plan, and durable agent
  notes; and
- passes the complete verification suite from a clean tree.

This slice does not:

- connect the structured frontend to `main()` or the production emitter;
- translate data-first declarations into prototype transfer-first text;
- introduce elastic RTL IR, emitted boundary adapters, or top-level
  specialization;
- parse pipeline field declarations (their data-first cutover remains with the
  pipeline migration);
- choose an unqualified-output abstraction;
- add initializers, aggregate declarations, unpacked slicing, or other
  declaration forms not already structurally supported; or
- add semantic support for ordinary SystemVerilog `byte` merely to replace the
  removed Pigen primitive.

## Surface grammar

The target declaration subset is below. Module items end at `;`; ANSI port
items end at their top-level `,` or the port-list `)`.

```text
signal-declaration ::=
    direction? data-type transfer-type? declarator-list
  | supported-systemverilog-static-declaration

direction       ::= "input" | "output" | "inout"

transfer-type   ::=
    "wire" | "reg" | "logic" | "buf" | "port" | "skid"
  | "fifo" "[" constant-expression "]"

declarator-list ::= declarator ("," declarator)*
declarator      ::= identifier declaration-dimension*
```

The declaration grammar uses the existing shared structural type parser. Type
arguments before the transfer occurrence belong to the data type. The transfer
catalogue recognizes the following token and states whether it accepts no
argument or one depth argument. The declarator parser begins only after the
complete transfer occurrence.

Examples include:

```systemverilog
int[16] buf left, right;
bit[8] wire mask;
packet_t fifo[DEPTH] queue[LANES], alternate[OTHER_LANES];

input int[16] sample;
input uint[8] opcode;
input bit flag;
input int[16] buf queued_sample;
output int[16] buf result;
```

Multiple declarators share the declaration's data type, transfer type, and
transfer argument. Each declarator owns its own ordered post-name shape.

A colonless bracket in a type or declarator dimension remains Pigen count
notation. A colon-bearing range retains its exact SystemVerilog left and right
bounds. Expression selection is unaffected.

Only `fifo` accepts a transfer argument, and it accepts exactly one bracketed
depth expression. A missing, empty, repeated, or otherwise malformed argument
is invalid when the surrounding declaration is unambiguously Pigen.

## Unified syntax representation

The syntax tree has one signal declaration kind and one signal declarator kind.
The declaration carries:

```text
direction
structural data-type syntax identity
optional written transfer-type occurrence
optional transfer-argument expression identity
ordered declarator children
```

The written transfer occurrence retains the transfer identity selected through
the transfer-type catalogue and its own source location. Absence is structural
information, not an inferred enum value. The transfer argument likewise retains
its own syntax identity and location.

Each declarator carries:

```text
name token
ordered post-name shape-dimension range
```

Delete `PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATION`,
`PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATOR`, their union members, their parser
helpers, and their resolution path. Ordinary statics and explicitly elastic
signals differ only in resolved transfer law, not in syntax topology.

The declaration parser is transactional. Speculative recognition which turns
out to be an unsupported ordinary SystemVerilog form restores syntax,
expression, type, and shape arenas before retaining the source as one opaque
region. Once a complete prefix makes a declaration unambiguously Pigen,
malformed trailing syntax is diagnosed rather than hidden as opaque text.

Transfer-first syntax is deleted rather than translated or recognized as a
legacy dialect. Tests and examples are replaced with the target spelling; no
compatibility fixture preserves the superseded form.

## Declaration policy ownership

Semantic resolution must not enumerate `int`, `uint`, `bit`, aliases, or future
primitive families. The data-type owner exposes one narrow unqualified-transfer
policy query whose result is a property rather than a concrete constructor:

```c
typedef enum {
	PIGEN_UNQUALIFIED_TRANSFER_STATIC,
	PIGEN_UNQUALIFIED_TRANSFER_ABSTRACT,
	PIGEN_UNQUALIFIED_TRANSFER_FORBIDDEN
} pigen_unqualified_transfer_policy;

pigen_unqualified_transfer_policy
pigen_data_type_unqualified_transfer_policy(
	const pigen_semantic_model *model,
	pigen_data_type_id data_type,
	int is_input);
```

Concrete data-type knowledge stays inside `data_type.c`, aliases follow their
resolved targets, and declaration resolution consumes only the returned
policy.

The initial policies are:

| Data-type family | Unqualified input | Unqualified non-input |
| --- | --- | --- |
| `int[n]`, `uint[n]`, aliases thereof | abstract | forbidden |
| `bit`, aliases thereof | abstract | supported SV static default |
| supported ordinary SV types | supported SV static default | supported SV static default |

Future Pigen data types extend this policy inside the data-type owner rather
than adding declaration-resolver branches.

## Semantic resolution

One declaration resolver replaces the static and nonstatic declaration
resolvers. It performs these steps:

1. Resolve the shared data-type syntax once.
2. If a transfer occurrence was written, obtain its descriptor and validate
   its parameter form and direction laws.
3. If no transfer occurrence was written, ask the data-type owner for the
   unqualified-transfer policy.
4. Resolve an accepted FIFO depth as a positive Pigen constant count using the
   exact Pigen literal domain.
5. Resolve each declarator's dimensions as constant expressions and intern its
   canonical shape.
6. Declare one symbol and add one unified semantic signal for each declarator.

Written concrete transfer types remain descriptor-owned. General declaration
code does not switch on `fifo`; it asks whether the descriptor requires a depth
argument. Dynamic transfer types reject `inout`. Supported static `inout`
retains ordinary SystemVerilog behavior.

An unqualified Pigen input receives `PIGEN_TRANSFER_TYPE_ABSTRACT`.
An explicit input transfer occurrence records the written concrete constraint
through the existing transfer identity until the later boundary-specialization
design introduces a separate constraint record. Both forms retain the uniform
consumer semantics already fixed by the signal model.

Unqualified Pigen internal signals fail because every such signal must state a
realization. Unqualified Pigen outputs fail because the output abstraction has
not been chosen. Pipeline fields are not an exception here because pipeline
declarations remain outside this slice and later supply their implicit `buf`
law through pipeline-owned declaration policy.

Supported ordinary SystemVerilog declarations retain the existing static
inference rules. This includes ordinary `logic`, `wire`, `reg`, internal/output
`bit`, explicit colon-bearing ranges, ANSI continuation, and declarator shapes.

## Pigen `byte` removal

Remove all Pigen-specific `byte` representation and policy:

- the private data-type constructor and descriptor;
- canonical construction and public lookup functions;
- byte-specific structural, conversion, capability, and operation branches;
- focused semantic and expression tests;
- source examples and documentation; and
- durable notes which call `byte` a Pigen data type.

Replace neutral byte-width examples with `bit[8]`. Do not add a deprecated
alias, compatibility constructor, fallback interpretation, or warning path.

The ordinary SystemVerilog spelling `byte` is not silently mapped onto
`bit[8]`: SystemVerilog `byte` has its own signed numerical meaning. It remains
opaque wherever that meaning is not yet implemented by the structured
frontend.

## Diagnostics and provenance

Every diagnostic uses the smallest owning syntax component:

- data-type errors point to the structural type occurrence;
- unknown or direction-incompatible transfer types point to the written
  transfer occurrence;
- missing, malformed, nonconstant, or nonpositive depth points to the transfer
  argument;
- invalid declarator shape points to its exact dimension;
- duplicate declarations point to the duplicate declarator name; and
- unsupported trailing syntax in an unmistakable Pigen declaration points to
  its first unsupported token.

An unsupported ordinary SystemVerilog form which the structured frontend does
not semantically consume stays opaque and lossless. Opaque text is never
searched to recover Pigen semantics.

## Invalid states removed by the cutover

After this change, the following states are not representable:

- distinct static and nonstatic declaration node families;
- a transfer-first Pigen declaration in the structured syntax tree;
- a FIFO depth stored as a payload type dimension;
- a transfer argument interpreted by general signal code rather than its
  descriptor;
- a Pigen internal or output signal with absent transfer semantics;
- a general resolver branch on a concrete Pigen data-type constructor; and
- any canonical Pigen `byte` identity.

## Verification

Focused syntax tests prove:

- all target data-first examples parse;
- every recognized runtime declaration uses one declaration/declarator node
  family;
- type arguments, FIFO depth, and declarator shapes occupy distinct fields;
- multiple internal and ANSI declarators preserve order and independent shape;
- colonless counts and colon-bearing ranges remain distinct; and
- unsupported ordinary SV, including ordinary `byte`, remains opaque without
  leaking partial arena state.

Focused semantic tests prove:

- every concrete transfer type resolves from data-first source;
- FIFO depth and declarator shape resolve independently;
- unqualified `int[n]`, `uint[n]`, `bit`, and Pigen-domain alias inputs are
  abstract;
- explicit input transfer constraints resolve;
- unqualified Pigen internal/output declarations fail;
- internal/output `bit` and supported ordinary SV declarations retain static
  inference;
- dynamic `inout`, invalid transfer arguments, nonconstant/nonpositive depth,
  unsupported initializers, and duplicate declarators fail at the owning span;
  and
- general declaration resolution contains no primitive-family enumeration.

Removal checks prove:

- no static-specific declaration node, parser helper, or resolver remains;
- no Pigen `byte` constructor, API, capability branch, test, example, or current
  note remains; and
- no transfer-first Pigen fixture remains outside historical design/plan
  documents.

Finally, `make clean && make verify` must pass. Because production integration
is deliberately out of scope, this gate proves the shared frontend cutover and
that the untouched production behavioral suite still passes; it does not claim
that `./pigen` accepts target declarations yet.

## Documentation cutover

Update `README.md` and `SPEC.md` to remove `byte`, specify `fifo[depth]`, and
record the contextual bare-`bit` input exception. Update `PLAN.md` to mark the
data-first declaration and structural-shape slice complete while leaving
generic boundary specialization, vertical lowering, and production integration
open. Compact `agent_notes/COMPILER_ARCHITECTURE.md`,
`agent_notes/SEMANTIC_INVARIANTS.md`, and `agent_notes/signal_model.md` around
the durable unified-declaration and no-`byte` laws.
