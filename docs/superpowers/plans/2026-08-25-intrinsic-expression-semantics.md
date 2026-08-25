# Intrinsic Expression Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace contextual Pigen expression sizing with exact literals, lossless intrinsic operation types, explicit semantic conversions, shared cast/declaration type syntax, and assignment-only destination conversion.

**Architecture:** Canonical arbitrary-precision integers and the data-type subsystem own exact values and intrinsic numerical laws. A temporary analyzed-expression arena resolves the complete tree without appending semantic expressions; a separate materializer commits one semantic tree with explicit non-identity conversions. Shared structural type syntax and type resolution serve both casts and declarations, while assignment and resource policies enter only at their explicit boundaries.

**Tech Stack:** C17, existing arena/ID-based semantic model, `make` unit targets, Icarus Verilog, Verilator

**Spec:** `docs/superpowers/specs/2026-08-25-intrinsic-expression-semantics-design.md`

## Global Constraints

- Pigen arithmetic is intrinsically lossless; destination context never changes an operator or intermediate result.
- Internal operands may widen but never narrow. Assignment and explicit cast are the only typed conversion boundaries.
- Same-family assignment narrowing is valid and quiet. Numerical-family or numerical/raw-vector changes require explicit cast.
- Unsized Pigen integers are exact values without a 32-bit or host-integer semantic width.
- Explicitly sized and based literals retain raw bit-vector meaning in Pigen expressions.
- Ordinary SystemVerilog retains its existing sizing, signedness, state, and assignment behavior.
- Concrete primitive knowledge remains in `src/data_type.c`; generic resolvers and walkers consume identities and decisions.
- No deprecated API, compatibility path, fallback, feature flag, runtime registry, external big-integer dependency, or CamelCase identifier.
- Every task follows red-green-refactor and ends in a focused commit.

---

## File structure

- `include/pigen/integer.h`, `src/integer.c`: canonical arbitrary-precision signed integers and exact arithmetic.
- `include/pigen/syntax_common.h`: source locations, syntax diagnostics, and signedness shared without include cycles.
- `include/pigen/type_syntax.h`, `src/type_syntax.c`: shared structural type arena and parser used by declarations and casts.
- `include/pigen/type_resolve.h`, `src/type_resolve.c`: one syntax-type-to-data-type resolution boundary.
- `include/pigen/expression_analysis.h`, `src/expression_analysis.c`: temporary intrinsic expression analysis.
- `src/expression_resolve.c`: public orchestration and analyzed-tree materialization; no primitive catalogue logic.
- `include/pigen/operation.h`, `include/pigen/data_type.h`, `src/data_type.c`: conversion vocabulary and operation/type-family policy.
- `include/pigen/semantic.h`, `src/semantic.c`: exact-literal, conversion, and resource-constraint semantic records.
- `include/pigen/expression.h`, `src/expression.c`, `include/pigen/syntax.h`, `src/syntax.c`: expression/type syntax ownership and parse integration.
- `include/pigen/resolve.h`, `src/resolve.c`: caller policy, typed assignments, and diagnostic propagation.
- `src/expression_use.c`: transparent reads through conversion nodes.
- `tests/integer_test.c`, `tests/semantic_test.c`, `tests/syntax_test.c`, `tests/expression_resolve_test.c`, `tests/expression_use_test.c`, `tests/resolve_test.c`: focused behavior and invariant coverage.
- `Makefile`: focused targets and source lists.
- `PLAN.md`, `agent_notes/COMPILER_ARCHITECTURE.md`, `agent_notes/SEMANTIC_INVARIANTS.md`: current roadmap and durable architectural knowledge.

---

### Task 1: Canonical exact-integer catalogue

**Files:**
- Create: `include/pigen/integer.h`
- Create: `src/integer.c`
- Create: `tests/integer_test.c`
- Modify: `include/pigen/ids.h`
- Modify: `include/pigen/semantic.h`
- Modify: `src/semantic.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `pigen_resize()` and the existing `PIGEN_INVALID_ID` convention.
- Produces: `pigen_integer_id`; canonical sign/magnitude storage in `pigen_semantic_model`; decimal parsing, comparison, negation, addition, subtraction, multiplication, bounded left shift, bounded nonnegative power, signed/unsigned bit-width queries, and limb access.

- [ ] **Step 1: Add failing arbitrary-precision catalogue tests**

Add `tests/integer_test.c` with assertions equivalent to:

```c
pigen_semantic_model model;
pigen_integer_id huge;
pigen_integer_id one;
pigen_integer_id negative;
pigen_integer_id sum;
pigen_integer_id product;

pigen_semantic_init(&model, &sources);
huge = pigen_integer_intern_decimal(&model,
	"1208925819614629174706175", 25);
one = pigen_integer_intern_decimal(&model, "1", 1);
assert(huge.index != PIGEN_INVALID_ID);
assert(huge.index == pigen_integer_intern_decimal(&model,
	"001208925819614629174706175", 27).index);
negative = pigen_integer_negate(&model, huge);
assert(pigen_integer_compare(&model, negative, huge) < 0);
sum = pigen_integer_add(&model, negative, huge);
assert(pigen_integer_is_zero(&model, sum));
product = pigen_integer_multiply(&model, huge, huge);
assert(pigen_integer_unsigned_width(&model, product) == 160);
assert(pigen_integer_signed_width(&model, negative) == 81);
assert(pigen_integer_shift_left(&model, one, 127, 128).index !=
	PIGEN_INVALID_ID);
assert(pigen_integer_shift_left(&model, one, 128, 128).index ==
	PIGEN_INVALID_ID);
assert(pigen_integer_power(&model, one, huge, 128).index == one.index);
```

Also assert canonical zero has no negative spelling, malformed decimal text is rejected, subtraction crosses zero correctly, multiplication signs are correct, and operations leave no output identity on resource-limit failure.

- [ ] **Step 2: Run the focused test and observe the missing interface**

Run: `make integer-test`

Expected: compilation fails because `pigen_integer_id`, `pigen/integer.h`, and the new functions do not exist.

- [ ] **Step 3: Add the integer identity and public catalogue interface**

Add `PIGEN_ID_TYPE(pigen_integer_id)` to `include/pigen/ids.h`. Define in `include/pigen/integer.h`:

```c
pigen_integer_id pigen_integer_intern_decimal(pigen_semantic_model *model,
	const char *digits, size_t length);
pigen_integer_id pigen_integer_intern_u64(pigen_semantic_model *model,
	uint64_t value);
pigen_integer_id pigen_integer_negate(pigen_semantic_model *model,
	pigen_integer_id value);
pigen_integer_id pigen_integer_add(pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right);
pigen_integer_id pigen_integer_subtract(pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right);
pigen_integer_id pigen_integer_multiply(pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right);
pigen_integer_id pigen_integer_shift_left(pigen_semantic_model *model,
	pigen_integer_id value, size_t amount, size_t maximum_bits);
pigen_integer_id pigen_integer_power(pigen_semantic_model *model,
	pigen_integer_id base, pigen_integer_id exponent, size_t maximum_bits);
int pigen_integer_compare(const pigen_semantic_model *model,
	pigen_integer_id left, pigen_integer_id right);
int pigen_integer_is_zero(const pigen_semantic_model *model,
	pigen_integer_id value);
size_t pigen_integer_unsigned_width(const pigen_semantic_model *model,
	pigen_integer_id value);
size_t pigen_integer_signed_width(const pigen_semantic_model *model,
	pigen_integer_id value);
```

Use canonical little-endian `uint32_t` limbs. Store `{negative, first_limb, limb_count}` records plus one shared limb arena in `pigen_semantic_model`. Canonical zero is nonnegative with zero limbs. Strip high zero limbs before interning and compare normalized sign/magnitude contents for identity.

- [ ] **Step 4: Implement exact arithmetic without external dependencies**

In `src/integer.c`, implement decimal accumulation as repeated magnitude `* 10 + digit`, sign-aware add/subtract, schoolbook limb multiplication with `uint64_t` temporaries, and exponentiation by squaring. Before allocation in shift/power, prove the result does not exceed `maximum_bits`; fail without interning an oversized value. `signed_width` returns the minimum two's-complement width, including the asymmetric negative boundary.

Initialize all integer counts to zero through the existing zeroed semantic model initialization and free both record and limb arrays in `pigen_free_semantic_model()`.

- [ ] **Step 5: Add and run the focused target**

Add `integer-test` to `.PHONY`, `test`, and `verify`, compiling `tests/integer_test.c` with `src/integer.c`, `$(SEMANTIC_SOURCES)`, `src/source.c`, and `src/util.c`.

Run: `make integer-test semantic-test`

Expected: both targets pass.

- [ ] **Step 6: Commit the exact-integer catalogue**

```bash
git add Makefile include/pigen/ids.h include/pigen/integer.h \
	include/pigen/semantic.h src/integer.c src/semantic.c tests/integer_test.c
git commit -m "add canonical exact integer catalogue"
```

---

### Task 2: Intrinsic Pigen numerical policy

**Files:**
- Modify: `include/pigen/operation.h`
- Modify: `include/pigen/data_type.h`
- Modify: `include/pigen/semantic.h`
- Modify: `src/data_type.c`
- Modify: `tests/semantic_test.c`

**Interfaces:**
- Consumes: exact `pigen_integer_id` values and canonical symbolic width expressions.
- Produces: `pigen_data_type_exact_integer()`, `pigen_data_type_exact_value()`, `PIGEN_NUMERICAL_EXACT_INTEGER`, `PIGEN_CONVERSION_EXACT_INTEGER`, `PIGEN_CONVERSION_INTEGER_PROMOTION`, and operation resolvers with no expected-result parameter.

- [ ] **Step 1: Replace contextual-policy tests with intrinsic-law tests**

In `tests/semantic_test.c`, delete assertions that an expected type widens an operation. Add exact assertions for `uint[8] + uint[8] -> uint[9]`, `int[8] + int[8] -> int[9]`, `int[8] + uint[8] -> int[10]`, `uint[8] - uint[8] -> int[9]`, both unary-negate laws, both eight-bit multiplication laws, `uint[8] + 1 -> uint[9]`, and `uint[8] * 3 -> uint[10]`.

Construct exact `0`, `1`, `3`, and `-1` data types through the new catalogue.
Assert exact zero does not force growth; mixed comparison widens losslessly and
returns `bit`; conditional alternatives form their range union; bitwise common
representation retains every bit; logical and every reduction return `bit`;
right shift retains the unshifted width; left shift and power create their
worst-case symbolic widths; byte arithmetic fails; and Pigen divide/modulo
fail. Exercise each unary and binary operator admitted for a Pigen numerical
family at least once. Assert the public resolver signatures no longer accept
an expected type.

- [ ] **Step 2: Run semantic tests and observe obsolete policy behavior**

Run: `make semantic-test`

Expected: compilation fails at the changed resolver calls and missing exact integer/promotion identities.

- [ ] **Step 3: Extend the semantic data-type algebra**

Add a private exact-integer constructor and a `pigen_integer_id exact_value` field to canonical data-type identity. Expose:

```c
pigen_data_type_id pigen_data_type_exact_integer(
	pigen_semantic_model *model, pigen_integer_id value);
pigen_integer_id pigen_data_type_exact_value(
	const pigen_semantic_model *model, pigen_data_type_id data_type);
```

Return `PIGEN_NUMERICAL_EXACT_INTEGER` from numerical interpretation. Exact types have no packed hardware width until represented by an operation, cast, or assignment; packed-width, selection, and concatenation queries reject them.

Add `PIGEN_CONVERSION_EXACT_INTEGER` for representing an exact value in a concrete target and `PIGEN_CONVERSION_INTEGER_PROMOTION` for lossless signed/unsigned common representation. Promotion is never used by assignment; reinterpretation remains explicit-cast-only.

Permit exact-to-`int`/`uint` assignment conversion through
`PIGEN_CONVERSION_EXACT_INTEGER`; the typed boundary may truncate an exact
value. Permit exact-to-numerical/raw conversion through explicit-cast policy.
Do not permit implicit concrete `int`/`uint` family changes.

- [ ] **Step 4: Remove expected-result operation interfaces**

Change the public signatures to:

```c
int pigen_data_type_resolve_unary_operation(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_data_type_id operand,
	pigen_unary_resolution *resolution);
int pigen_data_type_resolve_binary_operation(pigen_semantic_model *model,
	pigen_binary_operator operator, pigen_data_type_id left,
	pigen_data_type_id right, pigen_binary_resolution *resolution);
int pigen_data_type_resolve_conditional_operation(
	pigen_semantic_model *model, pigen_data_type_id condition,
	pigen_data_type_id when_true, pigen_data_type_id when_false,
	pigen_conditional_resolution *resolution);
```

Delete `expected_result_exists()`, `integer_expected_is_compatible()`, and every expected-width branch. Update internal width-algebra and ordinary-SystemVerilog callers to the singular signatures.

- [ ] **Step 5: Implement operation-owned result laws**

Replace `integer_common_data_type()` with private helpers that separately determine lossless operand representation, operator result family, symbolic result width, and exact singleton effects. Use canonical width sum/maximum plus exact magnitude queries. Fold exact-only `+`, `-`, `*`, unary `+`, and unary `-`; reject family-neutral operations whose meaning is not approved. Fill each operation record from conversion targets, preserve SystemVerilog and byte-alias laws, and leave output records untouched on failure.

- [ ] **Step 6: Run focused policy verification**

Run: `make semantic-test expression-resolve-test resolve-test`

Expected: `semantic-test` passes. Resolver targets may fail only where callers retain the removed expected argument or identity-only gate.

- [ ] **Step 7: Commit intrinsic numerical policy**

```bash
git add include/pigen/operation.h include/pigen/data_type.h \
	include/pigen/semantic.h src/data_type.c tests/semantic_test.c
git commit -m "derive intrinsic Pigen operation types"
```

---

### Task 3: Exact-literal and conversion semantic nodes

**Files:**
- Modify: `include/pigen/semantic.h`
- Modify: `src/semantic.c`
- Modify: `src/expression_use.c`
- Modify: `tests/semantic_test.c`
- Modify: `tests/expression_use_test.c`

**Interfaces:**
- Consumes: exact-integer data types and backend-neutral conversion records.
- Produces: `PIGEN_EXPR_EXACT_INTEGER`, `PIGEN_CONST_EXPR_EXACT_INTEGER`, `PIGEN_EXPR_CONVERSION`, `PIGEN_CONST_EXPR_CONVERSION`, and exact/conversion constructors for runtime and constant expressions.

- [ ] **Step 1: Add failing semantic-node tests**

In `tests/semantic_test.c`, create exact `3`, `uint[2]`, and `uint[8]`. Assert:

```c
exact_expr = pigen_expr_add_exact_integer(&model, exact_three, span);
converted = pigen_expr_add_conversion(&model,
	(pigen_conversion){PIGEN_CONVERSION_EXACT_INTEGER,
		exact_type, unsigned_2}, exact_expr, span);
resized = pigen_expr_add_conversion(&model,
	(pigen_conversion){PIGEN_CONVERSION_INTEGER_RESIZE,
		unsigned_2, unsigned_8}, converted, span);
assert(pigen_expr_get(&model, converted)->kind == PIGEN_EXPR_CONVERSION);
assert(pigen_expr_get(&model, converted)->data_type.index == unsigned_2.index);
assert(pigen_const_expr_get(&model,
	pigen_expr_constant(&model, converted))->kind ==
	PIGEN_CONST_EXPR_CONVERSION);
```

Assert identity, source-mismatched, invalid-target, and malformed conversions fail without increasing expression or constant-expression counts. Assert conversion preserves shape and cannot resolve as an lvalue. In `tests/expression_use_test.c`, wrap a signal read in a widening conversion and assert use analysis reports the underlying signal exactly once.

- [ ] **Step 2: Run tests and observe missing node kinds**

Run: `make semantic-test expression-use-test`

Expected: compilation fails because the new kinds and constructors do not exist.

- [ ] **Step 3: Add exact and conversion constant interning**

Extend constant equality with exact integer identity and full conversion identity `{kind, source, target, operand}`. Implement:

```c
pigen_const_expr_id pigen_const_expr_intern_exact_integer(
	pigen_semantic_model *model, pigen_integer_id value,
	pigen_data_type_id data_type);
pigen_const_expr_id pigen_const_expr_intern_conversion(
	pigen_semantic_model *model, pigen_conversion conversion,
	pigen_const_expr_id operand);
```

Require the exact data type to contain the same integer identity. Require a non-identity valid conversion, exact source match, and existing target.

- [ ] **Step 4: Add runtime exact and conversion construction**

Implement:

```c
pigen_expr_id pigen_expr_add_exact_integer(pigen_semantic_model *model,
	pigen_integer_id value, pigen_source_span span);
pigen_expr_id pigen_expr_add_conversion(pigen_semantic_model *model,
	pigen_conversion conversion, pigen_expr_id operand,
	pigen_source_span span);
```

The exact constructor obtains its type from `pigen_data_type_exact_integer()`. Conversion preserves operand shape, uses the target result type, and interns a constant conversion exactly when its operand is constant. Do not create a cast-specific or identity-conversion node.

- [ ] **Step 5: Update generic expression walkers**

Teach `src/expression_use.c` to recurse through `PIGEN_EXPR_CONVERSION` exactly once. Confirm lvalue resolution has no conversion case and therefore rejects it. Update every exhaustive expression/constant-kind switch to handle both new kinds without primitive-specific behavior.

- [ ] **Step 6: Run focused semantic verification**

Run: `make semantic-test predicate-test expression-use-test`

Expected: all pass.

- [ ] **Step 7: Commit semantic conversion structure**

```bash
git add include/pigen/semantic.h src/semantic.c src/expression_use.c \
	tests/semantic_test.c tests/expression_use_test.c
git commit -m "materialize semantic conversion expressions"
```

---

### Task 4: Shared structural type syntax and resolution

**Files:**
- Create: `include/pigen/syntax_common.h`
- Create: `include/pigen/semantic_error.h`
- Create: `include/pigen/type_syntax.h`
- Create: `src/type_syntax.c`
- Create: `include/pigen/type_resolve.h`
- Create: `src/type_resolve.c`
- Modify: `include/pigen/ids.h`
- Modify: `include/pigen/expression.h`
- Modify: `include/pigen/syntax.h`
- Modify: `include/pigen/data_type.h`
- Modify: `include/pigen/resolve.h`
- Modify: `src/expression.c`
- Modify: `src/syntax.c`
- Modify: `src/data_type.c`
- Modify: `src/resolve.c`
- Modify: `tests/syntax_test.c`
- Modify: `tests/expression_resolve_test.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: syntax expression identities for type arguments and constant type resolution.
- Produces: `pigen_syntax_type_id`, one shared type arena, structural count/range arguments, `pigen_parse_type_prefix()`, `pigen_resolve_type()`, and cast syntax that owns a type identity instead of an expression-as-type placeholder.

- [ ] **Step 1: Add failing shared-type and cast syntax tests**

Extend `tests/syntax_test.c` with `uint[8]'(value)`, `word_t'(value)`, and `logic [15:0]'(value)` plus an ordinary `typedef logic [7:0] word_t`. Assert declarations and casts reference valid `pigen_syntax_type_id` values in the same arena; `uint[8]` contains one count argument; `logic [15:0]` contains one range; the cast value is a separate expression child; and the cast span covers the entire form. Reject empty counts, malformed ranges, missing apostrophe parentheses, and expression-as-type forms.

- [ ] **Step 2: Run syntax tests and observe the old cast placeholder**

Run: `make syntax-model-test expression-resolve-test`

Expected: compilation fails because type identities and shared arena accessors do not exist.

- [ ] **Step 3: Introduce the shared type arena**

Add `pigen_syntax_type_id` to `ids.h`. In `type_syntax.h`, define:

```c
typedef enum {
	PIGEN_SYNTAX_TYPE_COUNT,
	PIGEN_SYNTAX_TYPE_RANGE
} pigen_syntax_type_argument_kind;

typedef struct {
	pigen_syntax_type_argument_kind kind;
	pigen_syntax_location location;
	union {
		pigen_syntax_expr_id count;
		struct { pigen_syntax_expr_id left, right; } range;
	} as;
} pigen_syntax_type_argument;

typedef struct {
	pigen_token_id base;
	pigen_syntax_signedness signedness;
	pigen_syntax_location location;
	size_t first_argument;
	size_t argument_count;
} pigen_syntax_type;
```

Move `pigen_syntax_location`, `pigen_syntax_error`, and
`pigen_syntax_signedness` into `syntax_common.h`; both expression and type
syntax include that header, while `type_syntax.h` otherwise needs only IDs.
The type arena owns records and ordered arguments. Move the current type struct
out of `syntax.h`; change declaration nodes and `PIGEN_SYNTAX_EXPR_CAST` to
store `pigen_syntax_type_id`. Free the arena once from
`pigen_free_syntax_tree()`.

- [ ] **Step 4: Extract one structural type parser**

Implement:

```c
int pigen_parse_type_prefix(const pigen_expanded_source *source,
	size_t first, size_t limit, pigen_syntax_expr_arena *expressions,
	pigen_syntax_type_arena *types, pigen_syntax_type_id *type,
	size_t *after, pigen_syntax_error *error);
```

Parse optional signedness, one base identifier, and ordered colonless counts or
colon-bearing ranges. Parse each bracket payload once into expression syntax.
The declaration adapter still separates a transfer parameter such as FIFO
depth from the payload-type extent, then calls the shared parser for that
extent; it no longer owns type grammar. Expression parsing recognizes
`type'(expression)` before ordinary postfix parsing and continues to allow
postfix selection of the cast result. Delete the expression-as-type cast path
and declaration-private type grammar.

- [ ] **Step 5: Centralize type semantic resolution**

Define the data-type-owned adapter:

```c
typedef enum {
	PIGEN_DATA_TYPE_ARGUMENT_COUNT,
	PIGEN_DATA_TYPE_ARGUMENT_RANGE
} pigen_data_type_argument_kind;

typedef struct {
	pigen_data_type_argument_kind kind;
	union {
		pigen_const_expr_id count;
		struct { pigen_const_expr_id left, right; } range;
	} as;
} pigen_data_type_argument;

pigen_data_type_id pigen_data_type_from_spelling(
	pigen_semantic_model *model, pigen_source_span spelling,
	pigen_signedness signedness, const pigen_data_type_argument *arguments,
	size_t argument_count);
```

The data-type owner recognizes `int[count]`, `uint[count]`, `bit`, `byte`,
ordinary primitive ranges, and compact count dimensions. `int`/`uint` require
exactly one positive integral count and reject signedness modifiers. No caller
switches on these spellings. A data-type-owned spelling-domain query tells the
shared resolver whether type-argument expressions use Pigen exact-literal or
ordinary SystemVerilog literal policy without exposing concrete constructors.

Extract `pigen_resolve_type()` from `src/resolve.c` into `src/type_resolve.c`.
It resolves argument expressions as constants, normalizes nonnegative exact
count values into the existing internal symbolic-width constant domain, asks
the data-type owner first, then resolves a typedef alias without reparsing
text. Symbolic integral bounds remain structural; negative and zero counts fail
at the type argument. Both declarations and casts call this service.
Delete `pigen_data_type_primitive_from_spelling()` after migrating its
consumers; do not retain parallel spelling-resolution APIs. Move the shared
semantic diagnostic record out of `resolve.h` into `semantic_error.h` and use
it directly from type, expression, and compilation-unit resolution.

- [ ] **Step 6: Update build edges and run parser/type tests**

Add `src/type_syntax.c` wherever expression or syntax parsing is linked, and `src/type_resolve.c` wherever semantic resolution is linked.

Run: `make syntax-model-test expression-resolve-test resolve-test`

Expected: all pass; existing ordinary packed ranges retain structural range identity.

- [ ] **Step 7: Commit shared type syntax**

```bash
git add Makefile include/pigen/ids.h include/pigen/syntax_common.h \
	include/pigen/semantic_error.h include/pigen/type_syntax.h \
	include/pigen/type_resolve.h include/pigen/expression.h \
	include/pigen/syntax.h include/pigen/data_type.h include/pigen/resolve.h \
	src/type_syntax.c \
	src/type_resolve.c src/expression.c src/syntax.c src/data_type.c \
	src/resolve.c tests/syntax_test.c tests/expression_resolve_test.c
git commit -m "share structural type syntax with casts"
```

---

### Task 5: Two-stage intrinsic expression resolver

**Files:**
- Create: `include/pigen/expression_analysis.h`
- Create: `src/expression_analysis.c`
- Modify: `include/pigen/expression_resolve.h`
- Modify: `src/expression_resolve.c`
- Modify: `tests/expression_resolve_test.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: shared type resolution, exact-integer types, intrinsic operation resolutions, and semantic constructors.
- Produces: temporary analyzed-expression arena, one materialization path, complete cast resolution, and removal of the identity-only gate.

- [ ] **Step 1: Add failing intrinsic-tree tests**

Extend `tests/expression_resolve_test.c` with semantic-only `int[8]`, `uint[8]`, and symbols using those types. Parse and resolve `(signed_left + signed_right) * signed_third`, `unsigned_left + 1`, `signed_left + unsigned_left`, `uint[8]'(signed_left)`, and `byte'(unsigned_left)`.

Assert the first tree has `int[17]` at its root and its addition child is
`int[9]`; its destination does not affect either type. Assert every
policy-required widening or mixed-family promotion is an explicit conversion
child, the literal has an exact node below its concrete operation conversion,
casts use explicit conversion kinds, and runtime/constant trees contain
matching conversion topology. Record `model.expression_count` before an
invalid raw/numeric tree and assert failure appends no semantic expressions.
Also assert an explicitly sized literal remains a raw bit-vector, an identity
cast materializes no conversion node, a cast-rooted expression cannot resolve
as an lvalue, and invalid operands/casts report the operator/cast span.

- [ ] **Step 2: Run resolver tests and observe the identity-only failure**

Run: `make expression-resolve-test`

Expected: the new expressions fail because non-identity decisions are rejected and cast resolution is absent.

- [ ] **Step 3: Define the temporary analyzed arena**

Add `pigen_analyzed_expr_id`. Reuse `pigen_semantic_error` from
`semantic_error.h`. In `expression_analysis.h`, define an arena whose nodes
store syntax identity, expression kind, source span, shape, resolved data type
or exact value, resolved operation and operand conversions, and analyzed
children. Expose:

```c
int pigen_analyze_expression(const pigen_syntax_tree *syntax,
	pigen_semantic_model *model, pigen_scope_id scope,
	pigen_syntax_expr_id expression, int constant_only,
	pigen_analyzed_expr_arena *arena, pigen_analyzed_expr_id *result,
	pigen_semantic_error *error);
void pigen_free_analyzed_expr_arena(pigen_analyzed_expr_arena *arena);
```

- [ ] **Step 4: Move recursive analysis out of semantic construction**

Move literal parsing, symbol lookup, operator mapping, and recursive shape/type checks from `src/expression_resolve.c` into `src/expression_analysis.c`. Unsized decimal literals intern exact integer values; explicitly sized/based literals retain bit-state parsing and raw vector types. Operator nodes call intrinsic data-type resolution and store complete decisions. Cast nodes resolve their target through `pigen_resolve_type()` and store explicit conversion policy. Analysis performs no `pigen_expr_add_*` call.

- [ ] **Step 5: Implement one semantic materializer**

Keep public orchestration in `src/expression_resolve.c`. Materialize children postorder, add every non-identity recorded conversion, then add the operation. Materialize exact literals through `pigen_expr_add_exact_integer()` and casts through the same conversion helper used by operator operands. Group, index, select, and concatenation retain existing constructors. Delete every identity-only decision rejection.

On analysis failure, free the arena without changing expression count. On materialization failure, report an internal semantic-construction failure; do not retry through another path.

- [ ] **Step 6: Run focused resolver and walker verification**

Run: `make expression-resolve-test expression-use-test predicate-test`

Expected: all pass.

- [ ] **Step 7: Commit two-stage expression resolution**

```bash
git add Makefile include/pigen/ids.h include/pigen/expression_analysis.h \
	include/pigen/expression_resolve.h src/expression_analysis.c \
	src/expression_resolve.c tests/expression_resolve_test.c
git commit -m "resolve intrinsic expressions in two stages"
```

---

### Task 6: Typed assignment boundaries and width constraints

**Files:**
- Modify: `include/pigen/semantic.h`
- Modify: `include/pigen/expression_resolve.h`
- Modify: `include/pigen/resolve.h`
- Modify: `src/semantic.c`
- Modify: `src/expression_analysis.c`
- Modify: `src/expression_resolve.c`
- Modify: `src/resolve.c`
- Modify: `tests/semantic_test.c`
- Modify: `tests/expression_resolve_test.c`
- Modify: `tests/resolve_test.c`

**Interfaces:**
- Consumes: intrinsic semantic expressions, assignment-conversion policy, and structural packed widths.
- Produces: caller-supplied `maximum_generated_bits`, known-width rejection, provenance-bearing symbolic constraints, assignment conversion above the complete RHS, and transfer final-type/shape invariants.

- [ ] **Step 1: Add failing resource and assignment tests**

Define test policy:

```c
const pigen_resolve_policy policy = {.maximum_generated_bits = 1024};
```

In `tests/expression_resolve_test.c`, assert a known `uint[8] << uint[11]` result exceeds policy and reports the shift operator without materializing an expression. Use a parameter-width shift whose bound cannot be evaluated and assert one semantic constraint records derived width, limit `1024`, and operator span.

In `tests/resolve_test.c`, assert assigning intrinsic `int[17]` arithmetic to `int[16]` stores one final resize above the whole RHS; assigning it to `int[24]` stores one widening conversion; assigning `int` to `uint` fails; explicitly casting to `uint` succeeds; and shape mismatch fails before transfer construction. In `tests/semantic_test.c`, call `pigen_transfer_add()` directly with unequal final data types and shapes and assert rejection.

- [ ] **Step 2: Run focused tests and observe missing boundary enforcement**

Run: `make semantic-test expression-resolve-test resolve-test`

Expected: tests fail because no policy/constraint interface or final transfer type check exists.

- [ ] **Step 3: Add resource policy and semantic constraints**

Define:

```c
typedef struct {
	size_t maximum_generated_bits;
} pigen_resolve_policy;

typedef struct {
	pigen_const_expr_id width;
	size_t maximum_bits;
	pigen_source_span span;
} pigen_width_constraint;
```

Store constraints in `pigen_semantic_model` and expose add/get accessors. Require positive policy input in `pigen_resolve_semantics()` and public expression resolution. Evaluate fully known structural widths through a fail-closed `pigen_const_expr_evaluate_u64()` helper. Reject known oversize results and append one canonical constraint for symbolic results.

- [ ] **Step 4: Materialize assignment conversions at the boundary**

Add:

```c
pigen_expr_id pigen_resolve_assignment_value(
	const pigen_syntax_tree *syntax, pigen_semantic_model *model,
	pigen_scope_id scope, pigen_syntax_expr_id expression,
	pigen_data_type_id target, const pigen_resolve_policy *policy,
	pigen_semantic_error *error);
```

It resolves the RHS intrinsically, requests `pigen_data_type_resolve_assignment_conversion()`, and appends only the final non-identity conversion. `resolve_assignment()` obtains destination lvalue type first and calls this function. It does not pass the target into analysis.

- [ ] **Step 5: Enforce transfer invariants in semantic construction**

In `pigen_transfer_add()`, load lvalue and value records and require exact final data-type and shape identity before allocating transfer storage. Keep the resolver's user-facing shape diagnostic, but make the semantic constructor fail closed independently.

- [ ] **Step 6: Run all structured frontend tests**

Run: `make source-test preprocess-test transfer-type-test syntax-model-test semantic-test integer-test predicate-test expression-resolve-test expression-use-test resolve-test`

Expected: all pass.

- [ ] **Step 7: Commit typed boundaries and constraints**

```bash
git add include/pigen/semantic.h include/pigen/expression_resolve.h \
	include/pigen/resolve.h src/semantic.c src/expression_analysis.c \
	src/expression_resolve.c src/resolve.c tests/semantic_test.c \
	tests/expression_resolve_test.c tests/resolve_test.c
git commit -m "enforce typed expression boundaries"
```

---

### Task 7: Consolidation and full verification

**Files:**
- Modify: `PLAN.md`
- Modify: `agent_notes/COMPILER_ARCHITECTURE.md`
- Modify: `agent_notes/SEMANTIC_INVARIANTS.md`
- Modify: any source or focused test file implicated by verification failure

**Interfaces:**
- Consumes: the complete approved intrinsic-expression implementation.
- Produces: synchronized roadmap/notes and evidence that the slice satisfies its spec without changing production SystemVerilog behavior.

- [ ] **Step 1: Audit implementation against every spec requirement**

Read the design spec linearly. For each governing law, resolver stage, conversion invariant, typed boundary, resource rule, diagnostic, compatibility rule, and verification item, identify its implementation and focused assertion. Add a failing focused test before code for any uncovered requirement.

- [ ] **Step 2: Remove superseded policy and dead paths**

Run:

```bash
rg -n "expected_result|integer_common_data_type|identity-only|expression-as-type" \
	include src tests agent_notes PLAN.md
rg -n "PIGEN_DATA_TYPE_(SIGNED_INTEGER|UNSIGNED_INTEGER|BYTE|EXACT_INTEGER)" \
	src --glob '!data_type.c'
```

Expected: no live code/current note retains contextual sizing, identity-only rejection, or cast placeholders; no primitive-constructor switch escapes `data_type.c`. Historical specs/plans may describe transitions.

- [ ] **Step 3: Update roadmap and compact durable notes**

In `PLAN.md`, record intrinsic expression typing, exact literals, conversions, casts, shared type syntax, typed assignments, and resource constraints as complete; keep data-first declarations and RTL lowering unchecked. Replace provisional paragraphs in both architecture notes with the implemented two-stage resolver and typed-boundary laws. Record Ari's 2026-08-25 ruling summary and prune repeated history.

- [ ] **Step 4: Run fresh full verification**

Run: `make clean && make verify`

Expected: every C compile, unit target, smoke suite, Icarus simulation, Verilator simulation, and compatibility target exits zero.

- [ ] **Step 5: Inspect the final diff and repository state**

Run:

```bash
git diff --check
git status --short
git diff --stat 1122f1f..HEAD
```

Expected: no whitespace errors, only intended files changed, and no generated binary or waveform is newly tracked.

- [ ] **Step 6: Commit final consolidation**

```bash
git add PLAN.md agent_notes/COMPILER_ARCHITECTURE.md \
	agent_notes/SEMANTIC_INVARIANTS.md
git commit -m "record intrinsic expression architecture"
```

- [ ] **Step 7: Request final code review**

Invoke `superpowers:requesting-code-review` against the complete implementation range. Address every correctness finding, rerun affected focused targets, then rerun `make verify` before claiming completion.
