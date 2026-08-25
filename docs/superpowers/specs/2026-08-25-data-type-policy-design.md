# Data-type policy design

## Purpose

The replacement frontend already owns canonical packed data types and resolved
operation records, but it currently accepts most operations only when their
operand identities are identical. That is not enough structure for Pigen's
`int[n]`, `uint[n]`, and `byte` types, and it would force expression resolution
or RTL lowering to rediscover promotion and conversion rules.

This change establishes the semantic policy boundary for primitive data types.
The data-type subsystem receives an operator, exact operand types, and an
optional expected result type. It alone decides whether the operation is
admissible, which effective operand types it uses, which conversions are
required, and the result type. Downstream consumers carry and execute that
decision without enumerating primitive types.

The initial policies are deliberately replaceable. David intends to evaluate
their naturalness by writing real Pigen and revise them with experience.
Replaceability means changing focused data-type policy and tests, not providing
runtime configuration, compatibility modes, or parallel semantics.

## Semantic families

Canonical data types remain opaque `pigen_data_type_id` values outside
`data_type.c`. Private constructors distinguish SystemVerilog packed values,
Pigen signed integers, Pigen unsigned integers, `byte`, the compiler's unsized
integer, and aliases. A focused public numerical-interpretation query exposes
only the distinction consumers genuinely need; it does not expose the private
constructor catalogue.

The initial Pigen catalogue has these meanings:

| Type | Width | State domain | Numerical interpretation |
| --- | --- | --- | --- |
| `bit` / `bit[n]` | one / `n` | two-state | SystemVerilog packed value |
| `byte` | eight | two-state | none; raw bit-vector |
| `int[n]` | `n` | two-state | signed integer |
| `uint[n]` | `n` | two-state | unsigned integer |

`byte` is not an unsigned integer in disguise. It supports selection,
concatenation, bitwise operations, equality, and logical use. Arithmetic and
ordered comparison require an explicit conversion to an integer type.

Ordinary SystemVerilog `logic`, packed `bit`, explicit signedness, and their
conversion rules remain a distinct compatibility contract. Pigen's stricter
integer-family policy must not silently replace SystemVerilog expression rules.

The state domains above are the initial policy, not hard-wired assumptions in
expression resolution or lowering. Their single owner is the primitive
data-type catalogue and its focused queries.

## Width representation

The width of `int[n]` and `uint[n]` is structural constant-expression identity,
not a host `size_t` copied from source text. This preserves parameterized widths
and lets later emission retain elaboration-time structure. The intrinsic width
belongs to the integer constructor; it is not reinterpreted as an arbitrary
additional packed dimension.

Width combination is canonical symbolic algebra owned by the constant/type
subsystems. The initial integer common width is the maximum of operand widths
and any compatible expected width. A narrower expected type therefore cannot
shrink the operation: narrowing remains a final contextual conversion.

## Conversion decisions

A resolved conversion records source type, target type, and a small
backend-neutral conversion identity. It contains no SystemVerilog syntax,
rendered cast, generated temporary, or source spelling.

The conversion identities distinguish at least:

- identity;
- integer resize, whose source interpretation determines sign or zero
  extension and whose target width determines truncation;
- explicit numerical reinterpretation between signed and unsigned integer
  families;
- vector/integer conversion used only by an explicit cast.

Implicit operation conversion is permitted within one Pigen integer family.
Mixed `int[n]`/`uint[m]` arithmetic and ordered comparison are rejected unless
an explicit cast first chooses the family. Assignment conversion is resolved
separately from operation conversion: it may resize a same-family integer, but
it does not retroactively narrow an operation's operands.

Conversion records are semantic decisions. The expression layer will
eventually materialize them as explicit conversion nodes before constructing
the operation node, so every operation record's effective operand identities
match its children. This first policy slice may establish the decision algebra
before source syntax calls it; it must not add an unused shadow validator to
the production compiler.

## Operation policy

There is no universal promotion routine. Operation resolution dispatches on
the operator and semantic family inside the data-type owner.

The initial Pigen integer policy is:

- unary arithmetic and bitwise operations preserve the integer family;
- same-family binary arithmetic and bitwise operands convert to a canonical
  common width;
- the common width is the symbolic maximum of both operand widths and a
  compatible expected width;
- an expected narrower result never narrows the operation; it is handled by a
  later assignment conversion;
- equal-width `int[n] * int[n]` and `uint[n] * uint[n]` therefore produce the
  lower `n` bits unless a wider compatible context requests a wider operation;
- relational, equality, and logical results use the canonical boolean type;
- shifts preserve the left operand's integer family and width; their count is
  an unsigned integer, independently of the left operand's signedness.

These are initial language policies rather than architectural invariants. A
future fixed-point family may give multiplication a result with a derived
fractional format and insert rescaling during contextual conversion. Changing
that rule remains local because callers consume resolved operations rather
than applying width or scaling laws themselves.

Raw `byte` addition is intentionally absent. Defining `+` as XOR for a future
Boolean-vector or `GF(2)` family is an amusing coherent possibility, but it is
not part of this design and receives no implementation scaffolding.

## Optional expected type

Expected type is advisory input to the owning policy, not an instruction to
coerce every operation. Each semantic family and operator decides whether and
how it participates. Integer arithmetic initially uses a compatible wider
integer expectation; a future family may ignore it, derive a different natural
result, or use it to select a lowering operation.

This keeps contextual typing modifiable without changing expression-tree
topology or backend interfaces. It also prevents a narrow assignment target
from changing the meaning of its right-hand arithmetic before the final
conversion.

## Lowering-facing record

Resolved unary, binary, and conditional operation records continue to carry
effective operand and result data types. They may later gain focused semantic
fields when a real family requires them, such as fixed-point rescaling. Do not
add speculative callbacks, registries, or unused overflow metadata.

A future `overflow(expression)` primitive should consume structural operation
or conversion semantics rather than reparse the expression or guess from
emitted widths. Its exact meaning is intentionally undecided and outside this
change.

## Errors and provenance

Policy functions fail closed for invalid type identities, zero widths,
unsupported operator/family pairs, and forbidden implicit cross-family
conversions. They return structural failure to the resolver; the resolver owns
the source span and eventual diagnostic wording. The data-type owner does not
fabricate provenance.

Unsupported mixed Pigen signedness is rejected at the operator. Unsupported
contextual conversion is rejected at the consuming assignment or cast. No
later lowering pass attempts to repair either case.

## Extension test

Adding or changing a primitive data type may require edits to:

1. its private constructor and canonical representation;
2. one primitive descriptor or focused constructor;
3. the operation/conversion policy genuinely specific to its family;
4. source-resolution and backend-boundary mappings where applicable;
5. focused tests and language documentation.

It must not require concrete-type branches in general expression walkers,
transfers, pipelines, FSMs, fabrics, RTL IR, or emission. A future semantic
family with genuinely new lowering behavior may extend the resolved operation
or conversion vocabulary at the data-type-to-RTL boundary.

## Verification

Focused tests will establish canonical integer and byte identities, symbolic
widths, initial two-state domains, numerical interpretation, permitted and
rejected operations, same-family common widths, optional-context widening,
non-narrowing contexts, shift-count rules, and explicit versus implicit
conversion decisions. Invalid identities and incomplete records fail closed.

The full `make verify` suite must remain green, including ordinary
SystemVerilog compatibility. This slice does not alter accepted source syntax,
production `main()`, or emitted RTL.

## Scope

The first implementation slice changes the data-type, operation, and canonical
constant-width algebra plus focused tests and notes. It introduces no target
declaration grammar, cast syntax, RTL IR, SystemVerilog rendering, production
cutover, overflow primitive, fixed-point type, runtime registry, fallback, or
compatibility mode.

Design organized by Ari with David on 2026-08-25.
