# Intrinsic Expression Semantics Design

**Date:** 2026-08-25

**Status:** approved for implementation

**Author:** Ari

## Purpose

Pigen arithmetic must describe the value computed by an expression before it
describes how that value fits into a destination. An expression therefore has
an intrinsic, lossless result type derived from its operator and operands. A
typed boundary such as an assignment or explicit cast may subsequently extend,
truncate, reinterpret, rescale, or round that result according to the boundary
type's policy.

This replaces the provisional policy in which an optional expected result type
could widen an operation. Destination context must not flow into a Pigen
arithmetic tree. For example, given three `int[8]` operands:

```text
a + b                 -> int[9]
(a + b) * c           -> int[17]
int[16] destination   <- one final truncating assignment conversion
```

The design also removes SystemVerilog's hidden default integer width from Pigen
integer literals, connects source casts to semantic conversion decisions, and
establishes the shared type-syntax boundary needed by data-first declarations.

## Scope

This slice provides:

- temporary analyzed expressions and canonical semantic materialization;
- exact, family-neutral Pigen integer literals;
- lossless, operation-owned numerical result types;
- lossless mixed `int`/`uint` operand promotion;
- explicit runtime and constant conversion nodes;
- assignment-boundary conversion;
- SystemVerilog-style `type'(expression)` casts;
- structural type syntax shared by casts and declarations;
- known-width and symbolic-width resource constraints;
- focused semantic, resolver, compatibility, and full-suite verification.

It does not expose data-first signal declarations, construct elastic RTL IR,
emit structured SystemVerilog, define fixed-point types, define `overflow`, or
admit Pigen division and remainder before their divide-by-zero behavior is
specified. Those are later slices over the boundaries established here.

## Governing laws

### Intrinsic expression meaning

A Pigen expression's arithmetic meaning is independent of its consumer. The
resolver never asks a destination what width an operator should use. Each
operator and participating data-type family jointly determine:

- the representation required for every operand;
- the lossless conversions into those representations;
- the result's numerical family and width;
- any operation semantics needed by lowering.

Operands may be widened but never narrowed inside an expression. Information
loss occurs only at an explicit typed boundary. A destination may not change an
operator's result type or cause an intermediate result to lose information.

### Range-derived numerical results

Every numerical data-type family exposes the representable range needed by its
private operation policy. An exact integer literal contributes a singleton
range. The operation policy derives the range of every mathematically valid
result and selects the narrowest canonical Pigen numerical type containing
that range. Family-specific closed formulae are preferred over a general
symbolic theorem prover, but they remain private to the data-type owner.

Required examples include:

| Expression | Intrinsic result |
| --- | --- |
| `uint[8] + uint[8]` | `uint[9]` |
| `int[8] + int[8]` | `int[9]` |
| `int[8] + uint[8]` | `int[10]` |
| `uint[8] - uint[8]` | `int[9]` |
| `-uint[8]` | `int[9]` |
| `-int[8]` | `int[9]` |
| `uint[8] * uint[8]` | `uint[16]` |
| `int[8] * int[8]` | `int[16]` |
| `uint[8] + 1` | `uint[9]` |
| `uint[8] * 3` | `uint[10]` |

Mixed signed and unsigned numerical operands are legal whenever the operation
owner can form a unique lossless common representation. It sign-extends signed
operands and zero-extends unsigned operands into that representation. A cast is
required only when no unique lossless numerical interpretation exists or when
crossing between numerical and raw-vector domains.

Bitwise operations form a lossless common representation and retain every
operand bit. Comparisons, logical operations, and reductions produce `bit`.
Right shifts retain enough width for the zero-shift case. Left shifts and powers
derive a worst-case lossless symbolic width from the count or exponent domain.
`byte` remains non-numerical and admits only structural, bitwise, equality, and
logical operations.

A conditional expression forms the union of its alternative ranges and chooses
the narrowest numerical result containing both. Its alternatives are widened
to that result without loss. Non-numerical alternatives require the data-type
owner's explicit compatibility law; the condition uses logical truth semantics
and the expression never inherits a destination type.

Pigen division and remainder remain fail-closed in source-visible Pigen
expressions until runtime divide-by-zero behavior is specified. Ordinary
SystemVerilog division and remainder retain ordinary SystemVerilog behavior.

### Exact integer literals

An unsized Pigen integer literal denotes its exact mathematical value. It has
no implicit 32-bit width and initially belongs to no signed or unsigned hardware
family. The analyzed form retains sign and canonical magnitude without a host
integer-width semantic limit. Negative literals arise through unary negation
but canonicalize to the same exact signed value identity.

An operation may consume an exact literal using its singleton range. A cast or
assignment may convert a literal directly to its destination type. A
literal-only constant subtree remains exact until it meets an operation with a
concrete Pigen numerical operand or a typed boundary.

Explicitly sized or based literals retain the width, state domain, and raw
bit-vector interpretation written in the source. They do not masquerade as
exact unsized integers, and numerical arithmetic on them requires an explicit
cast. Ordinary SystemVerilog interpretation of the same spellings remains
unchanged in ordinary SystemVerilog expressions.

Ordinary SystemVerilog literals retain SystemVerilog sizing and signedness.
When the structured frontend eventually resolves an ordinary SystemVerilog
expression, its SystemVerilog operation family—not the generic expression
walker—materializes the ordinary unsized-integer semantics. Pigen's exact
literal policy must not alter the permanent compatibility contract.

## Resolver architecture

Expression resolution has two stages:

```text
structured syntax -> temporary analyzed expression -> canonical semantic tree
```

### Analysis

The temporary analyzed tree does not append semantic expressions, lvalues, or
language objects to `pigen_semantic_model`. Data-type policy may perform its
ordinary idempotent canonical interning of widths and data-type identities
while analyzing an operation; unused canonical identities are catalogue
entries, not partial expression meaning. Each analyzed node retains the source
syntax identity and provenance, declarator shape, intrinsic type or unresolved
exact-literal identity, constant identity when available, and analyzed
children. Operator nodes retain the shared operator identity but do not
independently invent result types.

Analysis is bottom-up. The data-type subsystem consumes analyzed operand facts
and returns a complete resolution containing effective operand types,
conversions, result type, and operation record. No expected-result parameter
exists in an operation-resolution interface.

Keeping analysis temporary prevents append-only semantic storage from
accumulating abandoned nodes while types are being determined. Failure frees
the temporary tree and leaves no partial semantic expression graph.

### Materialization

Materialization walks one successfully analyzed tree and appends exactly one
canonical semantic tree. It first materializes children, then materializes each
recorded non-identity conversion, then constructs the operation whose effective
operand identities equal the converted child identities.

Identity conversions are omitted. Every stored conversion node therefore
denotes an actual semantic event. Operation constructors continue to validate
that operand data-type identities exactly match their resolved operation
record.

Runtime and canonical constant expressions have isomorphic conversion
structure. If an operand is constant, materializing its runtime conversion also
interns the corresponding constant conversion. Constantness may not be lost or
invented merely because a conversion was inserted.

## Semantic conversion nodes

A conversion expression stores:

- the backend-neutral `pigen_conversion` decision;
- its operand expression identity;
- its target data-type identity;
- the operand's preserved declarator shape;
- source provenance;
- its canonical constant-conversion identity when the operand is constant.

The conversion decision's source and target identities are authoritative.
Construction rejects an absent operand, an invalid decision, an identity
decision, a source mismatch, a nonexistent target, or a malformed constant
counterpart.

The current conversion vocabulary covers integer resize, integer
reinterpretation, vector-to-integer conversion, and integer-to-vector
conversion. Future fixed-point conversions may add rescaling and rounding
metadata to the decision without changing expression-tree topology or teaching
generic walkers about fixed-point constructors.

Conversion nodes are value expressions, never lvalues. Lvalue resolution may
pass through grouping, indexing, selection, and concatenation according to its
existing laws, but it must reject a conversion root or any converted writable
base.

## Typed boundaries

### Assignment

Assignment resolution proceeds in this order:

1. Resolve the destination solely as an lvalue and obtain its data type and
   declarator shape.
2. Analyze and materialize the complete intrinsic right-hand expression.
3. Ask the destination data-type family to resolve assignment conversion from
   the intrinsic result type.
4. Append a non-identity conversion node when required.
5. Require the final right-hand data type and declarator shape to equal the
   lvalue's data type and shape.
6. Construct the semantic transfer with that invariant already established.

Same-family integer narrowing is legal and quiet and retains the low target
bits. Same-family widening is exact and quiet. Changing numerical
interpretation, including `int` to `uint`, or crossing numerical/raw-vector
domains requires an explicit cast. Later fixed-point destination families own
their truncation, extension, rescaling, and rounding laws through the same
assignment-conversion boundary.

`pigen_transfer_add` validates final type and shape equality rather than
accepting an unchecked source. Lowering never repeats assignment compatibility
or conversion resolution.

### Explicit cast

Pigen uses the SystemVerilog-style spelling:

```systemverilog
uint[8]'(expression)
byte'(expression)
```

There is no C-style `(type)expression` form. A cast resolves its structural
target type through the shared type resolver, intrinsically resolves its value,
requests explicit-conversion policy, and materializes the decision as a
conversion node. An identity cast exists only during analysis: after successful
type checking, materialization omits it because it has no semantic effect.
There is one cast path and no identity conversion node.

## Shared structural type syntax

The current cast parser stores the apparent target as an expression identity.
That placeholder is removed. Casts and declarations instead reference the same
structural `pigen_syntax_type_id` arena.

A syntax type owns:

- its base spelling identity or explicit absence;
- signedness modifiers where ordinary SystemVerilog admits them;
- ordered structural type parameters or packed dimensions;
- its complete source location.

The expression and type arenas may reference each other through stable IDs:
type dimensions contain expression identities, while cast expressions contain
a type identity. Their C declarations must remain acyclic through an ID/common
syntax header rather than storing one form as the other.

One shared type-resolution service resolves primitive spellings, typedefs,
parameterized Pigen types, packed dimensions, and aliases. Declarations and
casts do not maintain separate spelling tables or reparse captured token text.
This is the direct prerequisite for the next data-first declaration slice.

## Resource constraints

Lossless sizing is never replaced by a silent cap. Left shift, power, or any
future operation may derive a very large result. The semantic result retains
the exact derived width, then resource policy validates it.

The data-type owner reports the structural width; it does not own a global
limit. Expression resolution receives a positive maximum-generated-bit policy
value from its caller. A fully known width above that value is a compile error
at the responsible operator. A parameter-dependent width produces a semantic
constraint `width <= maximum_generated_bits` carrying the same provenance.
Structured RTL emission later renders that constraint as an elaboration-time
failure. It may not clamp the width or choose a different result type.

The production CLI's default and user-facing configuration spelling are chosen
when the structured frontend enters `main()`. This slice establishes the narrow
policy input and semantic constraint representation without adding an unused
legacy-main option.

## Diagnostics and failure behavior

All resolution is fail-closed and reports the layer responsible for the
failure:

- invalid operand families or excessive known results report the operator;
- invalid explicit conversion reports the cast;
- invalid assignment conversion or shape reports the assignment;
- an unknown cast target reports the type spelling;
- malformed type parameters report the parameter or dimension;
- symbolic resource failure reports the original operator during elaboration.

Failure never substitutes SystemVerilog rules for a Pigen operation, silently
narrows an intermediate, reinterprets a signed value, or leaves a partial
semantic tree. Same-family assignment narrowing is ordinary valid behavior;
optional narrowing lint is outside language validity and outside this slice.

## Compatibility boundary

Pigen and ordinary SystemVerilog share syntax, semantic node shapes, and the
operation vocabulary, but not numerical policy. Concrete SystemVerilog sizing,
signedness, four-state behavior, and assignment conversion remain owned by the
SystemVerilog data-type family. Concrete Pigen arithmetic remains owned by the
Pigen numerical families. The expression resolver consumes both through the
same resolution records and never enumerates their catalogues.

The production textual compiler remains unchanged during this slice. Existing
ordinary-SystemVerilog tests must remain byte-for-byte behaviorally compatible.
No fallback, dual Pigen syntax, or compatibility mode is introduced.

## Verification

Focused tests must prove:

- the approved unary, addition, subtraction, multiplication, bitwise,
  comparison, logical, reduction, conditional, shift, and power result laws;
- lossless mixed signed/unsigned promotion;
- exact literals do not acquire a hidden 32-bit type and contribute their
  actual singleton range;
- explicitly sized and based Pigen literals retain raw bit-vector meaning;
- nested arithmetic has the same intrinsic semantic tree under differently
  typed destinations;
- destination conversion appears only above the complete intrinsic RHS;
- runtime and constant conversions are structurally equivalent;
- casts use shared type syntax and explicit conversion policy;
- casts cannot produce lvalues;
- transfer construction rejects unequal final type or shape;
- known excessive widths fail at resolution;
- symbolic excessive widths survive as provenance-bearing constraints;
- byte and raw-vector arithmetic remain rejected without casts;
- invalid records and identities fail without partial output;
- ordinary SystemVerilog compatibility remains unchanged.

Verification ends with a fresh `make verify`. Tests for superseded contextual
Pigen sizing are replaced rather than retained as regression compatibility.

## Clean replacement and handoff

Implementation removes the optional expected-result field from unary, binary,
and conditional operation resolution and replaces the contextual-width policy
outright. It removes the expression resolver's identity-only conversion gate
and the expression-as-type cast placeholder. It retains no deprecated API,
alternate resolver, feature flag, or fallback.

When this slice is verified, expression semantics and shared type syntax are
ready for source-visible `int[n]`, `uint[n]`, `bit`, and `byte` data-first
declarations. That next slice feeds the first semantic-to-elastic-RTL vertical
slice, after which structured behavior can begin displacing production
`main()` behavior.
