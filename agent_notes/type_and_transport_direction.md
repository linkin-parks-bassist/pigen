# Data types and movement policy

## Design direction (2026-08-20)

Declarations should separate two independent concerns:

```text
data type    transport type    name and array shape
```

For example:

```systemverilog
int[16] buf x;
int[16] buf arr[8];
```

The order is significant and should remain exactly as above.  The data type
describes the value; the following transport type describes how values move,
wait, pulse, or apply backpressure; the declarator names the object and may add
array dimensions.  The semantic model must not encode combinations such as
"16-bit signed buffer" as indivisible kinds.  It should refer independently to
a data-type identity and a movement-policy identity.

`transport` is the current project term for the second axis, but David does not
particularly like the name and did not choose it.  Treat the terminology as
replaceable.  Do not let public spelling leak into semantic type names or make
a later rename architecturally expensive.

## Width and extent shorthand

A colonless bracket expression is intended as a count rather than a literal
SystemVerilog index or bound:

```systemverilog
int[16]       -> 16-bit signed integer
uint[9]       -> 9-bit unsigned integer
arr[8]        -> eight array elements
```

Its lowering rule is:

```text
[X] -> [X - 1:0]
```

The grammar position determines what the count shapes.  A bracket group on the
data type is a packed value width; a bracket group after the declarator is an
array extent.  The compiler should represent both as structured extent
expressions and lower them only after parsing; this must not be implemented by
a global textual substitution.

Colon-bearing ordinary SystemVerilog dimensions remain ordinary SystemVerilog
and retain their spelling and meaning.  In particular:

```systemverilog
logic [21:0] h[0:12];
```

must continue to mean exactly what it means in SystemVerilog today.  Pigen's
count syntax is additive convenience, not a reinterpretation of explicit
ranges.

David is willing to make the clean language break that an otherwise accepted
colonless `[X]` denotes this count shorthand.  The eventual specification must
state the precise syntactic boundary of that rule.  In particular, decide
whether it applies throughout ordinary SystemVerilog or only inside declaration
forms claimed by Pigen.  The latter preserves the stronger ordinary-SV
compatibility contract; the former is a deliberate documented exception to
it.  Do not choose silently during implementation.

## Initial data-type vocabulary

The desired initial set is:

- `int[n]`: an `n`-bit signed integer;
- `uint[n]`: an `n`-bit unsigned integer;
- `bit`: a single bit;
- `byte`: an eight-bit value.

This set is intentionally small and expected to grow.  Data types should
therefore be represented by a proper data-type algebra rather than switches
distributed across parsers, analyses, and emitters.  Width is a structural
expression belonging to the data type.  Array extents belong to the declarator
shape and do not alter the element data type.

The familiar packed/unpacked distinction may remain necessary in the emitted
SystemVerilog and in layout semantics, but it need not dominate Pigen's surface
language.  The surface distinction can be phrased more directly as value width
before the movement policy and array shape after the name.

## Compatibility and lowering boundary

Pigen declarations and ordinary SystemVerilog declarations must coexist.  A
colon-bearing declaration such as the `logic` example above stays on the
ordinary-SystemVerilog path.  A declaration using the ordered Pigen form is
parsed structurally and lowered to equivalent SystemVerilog ranges, signedness,
storage, and ready/valid machinery.

The backend spelling is not the semantic model.  For example, `int[16]` may
eventually emit a suitable `logic signed [15:0]`, but analyses should consume a
resolved signed 16-bit data-type object rather than that rendered string.

This direction supersedes the present transport-first examples such as:

```systemverilog
buf signed [15:0] sample;
fifo packet_t[8] messages;
```

There is no legacy Pigen syntax to preserve.  Once specified and implemented,
the new order must replace the old syntax across the compiler, specification,
examples, and tests; there should be no dual grammar or fallback.

## Decisions deliberately left open

Before implementation, David should choose:

- whether `bit` and the integer family have two-state or four-state simulation
  semantics;
- whether `byte` is signed, unsigned, or requires signed and unsigned forms;
- whether width zero and nonconstant or negative width expressions are rejected
  syntactically, semantically, or at elaboration;
- whether additional array dimensions each use count syntax and whether an
  explicit direction syntax is also wanted;
- how typedefs, structs, enums, and user-defined data types fit before the
  movement-policy position;
- the replacement, if any, for the word `transport`.
