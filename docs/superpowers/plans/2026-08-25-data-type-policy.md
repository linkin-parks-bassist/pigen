# Data-type Policy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add canonical Pigen integer and byte semantics plus backend-neutral conversion and operation decisions without changing accepted syntax or production lowering.

**Architecture:** `src/data_type.c` remains the only owner of private primitive constructors and operator/type-family policy. Canonical constant expressions gain symbolic width maximum, while `operation.h` carries policy results as conversion records wrapped around effective operation records. Existing expression callers continue to construct only identity-conversion operations; a later slice will materialize non-identity conversions as semantic expression nodes.

**Tech Stack:** C17, assert-based focused C tests, GNU Make verification.

**Spec:** `docs/superpowers/specs/2026-08-25-data-type-policy-design.md`

## Global Constraints

- Canonical data types remain opaque `pigen_data_type_id` values outside `src/data_type.c`.
- `byte` is a two-state eight-bit vector with no numerical interpretation; arithmetic and ordered comparison require an explicit integer conversion.
- Pigen `int[n]` and `uint[n]` are initially two-state and do not mix implicitly.
- Operation and type family jointly own effective operands, conversions, and result type.
- Optional expected type may widen compatible integer arithmetic but may not narrow the operation.
- Ordinary SystemVerilog compatibility remains unchanged.
- Do not add syntax, production `main()` integration, RTL IR, emission, fixed point, overflow support, registries, fallbacks, or compatibility modes.
- Use fully lowercase snake_case except uppercase preprocessor-style and enum constants.

---

### Task 1: Canonical symbolic width maximum

**Files:**
- Modify: `include/pigen/semantic.h`
- Modify: `src/semantic.c`
- Test: `tests/semantic_test.c`

**Interfaces:**
- Consumes: canonical `pigen_const_expr_id` values and the existing sequence interner used by width sums and products.
- Produces: `PIGEN_CONST_EXPR_WIDTH_MAXIMUM` and `pigen_const_expr_intern_width_maximum(pigen_semantic_model *, const pigen_const_expr_id *, size_t)`.

- [ ] **Step 1: Write failing canonicalization tests**

Add width identities near the existing width-product assertions in `tests/semantic_test.c`. Construct constants 8 and 16 using the canonical unsized-integer type, then assert:

```c
pigen_const_expr_id width_values[3];
pigen_const_expr_id width_maximum;
pigen_const_expr_id reordered_width_maximum;

width_values[0] = pigen_const_expr_intern_integer(&model, 8,
	unsized_integer_data_type);
width_values[1] = pigen_const_expr_intern_integer(&model, 16,
	unsized_integer_data_type);
width_values[2] = width_values[0];
width_maximum = pigen_const_expr_intern_width_maximum(&model,
	width_values, 3);
assert(pigen_const_expr_get(&model, width_maximum)->kind ==
	PIGEN_CONST_EXPR_INTEGER);
assert(pigen_const_expr_get(&model, width_maximum)->as.integer == 16);
width_values[0] = pigen_const_expr_intern_select_width(&model,
	pigen_expr_constant(&model, left_bound),
	pigen_expr_constant(&model, right_bound),
	PIGEN_SEMANTIC_SELECT_RANGE);
width_values[1] = pigen_const_expr_intern_integer(&model, 8,
	unsized_integer_data_type);
reordered_width_maximum = pigen_const_expr_intern_width_maximum(&model,
	width_values, 2);
width_values[0] = pigen_const_expr_intern_integer(&model, 8,
	unsized_integer_data_type);
width_values[1] = pigen_const_expr_intern_select_width(&model,
	pigen_expr_constant(&model, left_bound),
	pigen_expr_constant(&model, right_bound),
	PIGEN_SEMANTIC_SELECT_RANGE);
assert(reordered_width_maximum.index ==
	pigen_const_expr_intern_width_maximum(&model, width_values, 2).index);
```

Also assert that one child returns that child, repeated identities are removed, nested maxima flatten, zero children fail, and an invalid child fails.

- [ ] **Step 2: Run the focused test and confirm RED**

Run: `make semantic-test`

Expected: compilation fails because `pigen_const_expr_intern_width_maximum` and `PIGEN_CONST_EXPR_WIDTH_MAXIMUM` do not exist.

- [ ] **Step 3: Add the width-maximum identity**

In `include/pigen/semantic.h`, add `PIGEN_CONST_EXPR_WIDTH_MAXIMUM` beside width sum/product and declare:

```c
pigen_const_expr_id pigen_const_expr_intern_width_maximum(
	pigen_semantic_model *model, const pigen_const_expr_id *values,
	size_t count);
```

In `src/semantic.c`:

- include maximum in sequence equality and `intern_const_sequence()` validation;
- generalize `collect_width_terms()` and `intern_width_sequence()` to accept maximum;
- flatten nested maximum nodes;
- remove duplicate canonical child identities;
- fold all literal-integer members into their greatest literal;
- discard a literal zero when another member exists;
- sort remaining child identities before interning;
- return the sole remaining child directly;
- reject zero children and invalid child identities.

Keep sum/product behavior unchanged. Maximum is commutative, associative, and idempotent; sum/product are commutative and associative but must retain multiplicity.

- [ ] **Step 4: Run focused verification**

Run: `make semantic-test expression-resolve-test resolve-test`

Expected: all three targets print `PASS` and exit zero.

- [ ] **Step 5: Commit the width algebra**

```bash
git add include/pigen/semantic.h src/semantic.c tests/semantic_test.c
git commit -m "add canonical width maximum algebra"
```

### Task 2: Canonical Pigen primitive families and conversions

**Files:**
- Modify: `include/pigen/data_type.h`
- Modify: `include/pigen/operation.h`
- Modify: `src/data_type.c`
- Test: `tests/semantic_test.c`

**Interfaces:**
- Consumes: canonical width expressions from Task 1.
- Produces:
  - `pigen_numerical_interpretation`;
  - `pigen_conversion_kind` and `pigen_conversion`;
  - `pigen_data_type_signed_integer()`;
  - `pigen_data_type_unsigned_integer()`;
  - `pigen_data_type_byte()`;
  - `pigen_data_type_numerical_interpretation()`;
  - `pigen_data_type_resolve_assignment_conversion()`;
  - `pigen_data_type_resolve_explicit_conversion()`.

- [ ] **Step 1: Write failing primitive-family tests**

In `tests/semantic_test.c`, construct width constants 8, 12, and 16. Add canonical-type assertions equivalent to:

```c
pigen_data_type_id signed_8 = pigen_data_type_signed_integer(&model, width_8);
pigen_data_type_id same_signed_8 =
	pigen_data_type_signed_integer(&model, width_8);
pigen_data_type_id signed_16 =
	pigen_data_type_signed_integer(&model, width_16);
pigen_data_type_id unsigned_8 =
	pigen_data_type_unsigned_integer(&model, width_8);
pigen_data_type_id byte = pigen_data_type_byte(&model);

assert(signed_8.index == same_signed_8.index);
assert(signed_8.index != signed_16.index);
assert(signed_8.index != unsigned_8.index);
assert(pigen_data_type_numerical_interpretation(&model, signed_8) ==
	PIGEN_NUMERICAL_SIGNED_INTEGER);
assert(pigen_data_type_numerical_interpretation(&model, unsigned_8) ==
	PIGEN_NUMERICAL_UNSIGNED_INTEGER);
assert(pigen_data_type_numerical_interpretation(&model, byte) ==
	PIGEN_NUMERICAL_NONE);
assert(pigen_data_type_state_domain(&model, signed_8) ==
	PIGEN_DATA_TYPE_STATE_TWO);
assert(pigen_data_type_state_domain(&model, unsigned_8) ==
	PIGEN_DATA_TYPE_STATE_TWO);
assert(pigen_data_type_state_domain(&model, byte) ==
	PIGEN_DATA_TYPE_STATE_TWO);
assert(pigen_const_expr_get(&model,
	pigen_data_type_packed_width(&model, byte))->as.integer == 8);
```

Assert aliases preserve the target's numerical interpretation and intrinsic width. Assert zero, invalid, or non-integral width expressions fail construction. Assert packed element/select on integer and byte types yields ordinary two-state packed values rather than retaining integer-family identity.

- [ ] **Step 2: Write failing conversion-policy tests**

Add `pigen_conversion conversion;` and assert:

```c
assert(pigen_data_type_resolve_assignment_conversion(&model,
	signed_8, signed_16, &conversion));
assert(conversion.kind == PIGEN_CONVERSION_INTEGER_RESIZE);
assert(conversion.source_data_type.index == signed_8.index);
assert(conversion.target_data_type.index == signed_16.index);
assert(!pigen_data_type_resolve_assignment_conversion(&model,
	signed_8, unsigned_8, &conversion));
assert(!pigen_data_type_resolve_assignment_conversion(&model,
	byte, unsigned_8, &conversion));
assert(pigen_data_type_resolve_explicit_conversion(&model,
	signed_8, unsigned_8, &conversion));
assert(conversion.kind == PIGEN_CONVERSION_INTEGER_REINTERPRET);
assert(pigen_data_type_resolve_explicit_conversion(&model,
	byte, unsigned_8, &conversion));
assert(conversion.kind == PIGEN_CONVERSION_VECTOR_TO_INTEGER);
assert(pigen_data_type_resolve_explicit_conversion(&model,
	unsigned_8, byte, &conversion));
assert(conversion.kind == PIGEN_CONVERSION_INTEGER_TO_VECTOR);
```

Also test identity conversion, explicit resize plus reinterpretation across unequal widths, byte-to-integer widths other than eight, invalid identities, and null output pointers. An explicit vector/integer conversion may resize, so the record's source and target types remain the complete lowering contract; no second width-conversion record is needed.

- [ ] **Step 3: Run the focused test and confirm RED**

Run: `make semantic-test`

Expected: compilation fails because the Pigen constructors, numerical interpretation, and conversion vocabulary do not exist.

- [ ] **Step 4: Add semantic-family and conversion interfaces**

In `include/pigen/data_type.h`, add:

```c
typedef enum {
	PIGEN_NUMERICAL_INVALID,
	PIGEN_NUMERICAL_NONE,
	PIGEN_NUMERICAL_SIGNED_INTEGER,
	PIGEN_NUMERICAL_UNSIGNED_INTEGER,
	PIGEN_NUMERICAL_SYSTEMVERILOG
} pigen_numerical_interpretation;

pigen_data_type_id pigen_data_type_signed_integer(
	pigen_semantic_model *model, pigen_const_expr_id width);
pigen_data_type_id pigen_data_type_unsigned_integer(
	pigen_semantic_model *model, pigen_const_expr_id width);
pigen_data_type_id pigen_data_type_byte(pigen_semantic_model *model);
pigen_numerical_interpretation pigen_data_type_numerical_interpretation(
	const pigen_semantic_model *model, pigen_data_type_id data_type);
int pigen_data_type_resolve_assignment_conversion(
	const pigen_semantic_model *model, pigen_data_type_id source,
	pigen_data_type_id target, pigen_conversion *conversion);
int pigen_data_type_resolve_explicit_conversion(
	const pigen_semantic_model *model, pigen_data_type_id source,
	pigen_data_type_id target, pigen_conversion *conversion);
```

In `include/pigen/operation.h`, add the backend-neutral record:

```c
typedef enum {
	PIGEN_CONVERSION_INVALID,
	PIGEN_CONVERSION_IDENTITY,
	PIGEN_CONVERSION_INTEGER_RESIZE,
	PIGEN_CONVERSION_INTEGER_REINTERPRET,
	PIGEN_CONVERSION_VECTOR_TO_INTEGER,
	PIGEN_CONVERSION_INTEGER_TO_VECTOR
} pigen_conversion_kind;

typedef struct {
	pigen_conversion_kind kind;
	pigen_data_type_id source_data_type;
	pigen_data_type_id target_data_type;
} pigen_conversion;
```

- [ ] **Step 5: Extend canonical representation privately**

In `src/data_type.c`:

- add private signed-integer, unsigned-integer, and byte constructors;
- add `pigen_const_expr_id intrinsic_width` to the canonical record and include it in interning identity;
- require an integral-typed constant expression for parameterized integer
  widths, reject a literal zero immediately, and retain symbolic parameter
  widths for later elaboration validation;
- keep the new constructors out of spelling lookup, because target grammar is outside this slice;
- give `byte` a fixed descriptor width of eight, two-state domain, and no arithmetic capability;
- make `pigen_data_type_packed_width()` multiply intrinsic width by any alias/packed dimensions;
- make packed projection of an intrinsic multi-bit family return ordinary two-state `bit` identities, not an integer or byte identity;
- follow aliases for numerical interpretation and conversion classification.

Do not make callers inspect constructors. The signedness query may report signed/unsigned for integer families, but numerical interpretation is authoritative for arithmetic; byte must not acquire integer capability merely because it has no sign bit.

- [ ] **Step 6: Implement fail-closed conversion policy**

Implement identity, same-family integer assignment resize, explicit integer-family reinterpretation, and explicit byte/integer conversion exactly as tested. Construct the complete record only on success. Reject implicit byte/integer conversion, implicit signed/unsigned crossing, invalid identities, and null output.

Keep SystemVerilog conversion behavior unchanged: identity remains accepted, and this slice does not broaden or replace the existing exact-type structured assignment path.

- [ ] **Step 7: Run focused verification**

Run: `make semantic-test predicate-test expression-resolve-test expression-use-test resolve-test`

Expected: all five targets print `PASS` and exit zero.

- [ ] **Step 8: Commit primitive families and conversions**

```bash
git add include/pigen/data_type.h include/pigen/operation.h src/data_type.c tests/semantic_test.c
git commit -m "add canonical Pigen data type families"
```

### Task 3: Type-family-owned operation resolution

**Files:**
- Modify: `include/pigen/operation.h`
- Modify: `include/pigen/data_type.h`
- Modify: `src/data_type.c`
- Modify: `src/expression_resolve.c`
- Modify: `tests/semantic_test.c`
- Modify: `tests/expression_resolve_test.c`
- Modify: `agent_notes/COMPILER_ARCHITECTURE.md`
- Modify: `agent_notes/SEMANTIC_INVARIANTS.md`
- Modify: `PLAN.md`

**Interfaces:**
- Consumes: primitive-family identities and conversions from Task 2; symbolic maximum from Task 1.
- Produces `pigen_unary_resolution`, `pigen_binary_resolution`, and `pigen_conditional_resolution`, each containing required conversions plus the existing effective operation record.

- [ ] **Step 1: Write failing integer-operation policy tests**

In `tests/semantic_test.c`, build signed widths 8, 12, and 16 plus unsigned widths 8 and 12. Assert:

- signed 8 plus signed 12 resolves both operands and the result to signed `max(8, 12)`;
- unsigned 8 bitwise-and unsigned 12 uses unsigned `max(8, 12)`;
- signed 8 times signed 8 remains signed 8 without expected type;
- signed 8 times signed 8 with expected signed 16 converts both operands to signed 16 and returns signed 16;
- signed 16 times signed 16 with expected signed 8 remains signed 16, leaving narrowing to assignment conversion;
- signed/unsigned add and ordered comparison fail;
- signed left shift by unsigned 12 succeeds and preserves signed 8;
- signed left shift by signed 12 fails;
- integer relational, equality, and logical operations return boolean;
- invalid expected type is treated as absence only when it is exactly `INVALID_ID(pigen_data_type_id)`; a non-existent non-invalid identity fails closed.

Inspect every returned `pigen_conversion` source and target as well as the effective operation types. Equal source/target pairs must use identity conversion; resized pairs must use integer resize.

- [ ] **Step 2: Write failing byte-operation tests**

Assert byte bitwise-and byte returns byte, byte equality and logical use return boolean, and byte concatenation/selection behavior remains structural. Assert byte addition, multiplication, ordered comparison, arithmetic unary negate, and mixed byte/integer operations fail. Bitwise-not and reductions on byte remain permitted.

- [ ] **Step 3: Run the focused test and confirm RED**

Run: `make semantic-test`

Expected: compilation fails because resolution records and expected-type-aware APIs do not exist.

- [ ] **Step 4: Add resolution records and clean-break signatures**

In `include/pigen/operation.h`, add:

```c
typedef struct {
	pigen_conversion operand_conversion;
	pigen_unary_operation operation;
} pigen_unary_resolution;

typedef struct {
	pigen_conversion left_conversion;
	pigen_conversion right_conversion;
	pigen_binary_operation operation;
} pigen_binary_resolution;

typedef struct {
	pigen_conversion condition_conversion;
	pigen_conversion when_true_conversion;
	pigen_conversion when_false_conversion;
	pigen_conditional_operation operation;
} pigen_conditional_resolution;
```

Replace the three resolver declarations in `include/pigen/data_type.h` with:

```c
int pigen_data_type_resolve_unary_operation(pigen_semantic_model *model,
	pigen_unary_operator operator, pigen_data_type_id operand,
	pigen_data_type_id expected_result, pigen_unary_resolution *resolution);
int pigen_data_type_resolve_binary_operation(pigen_semantic_model *model,
	pigen_binary_operator operator, pigen_data_type_id left,
	pigen_data_type_id right, pigen_data_type_id expected_result,
	pigen_binary_resolution *resolution);
int pigen_data_type_resolve_conditional_operation(
	pigen_semantic_model *model, pigen_data_type_id condition,
	pigen_data_type_id when_true, pigen_data_type_id when_false,
	pigen_data_type_id expected_result,
	pigen_conditional_resolution *resolution);
```

`INVALID_ID(pigen_data_type_id)` means no expectation.

- [ ] **Step 5: Implement local operation-family dispatch**

In `src/data_type.c`, retain ordinary code rather than a runtime registry:

- preserve current SystemVerilog packed-type behavior when operand identities already satisfy it;
- classify arithmetic, shift, ordered, equality, bitwise, logical, and reduction operator groups with private helpers;
- resolve same-family Pigen integer common width through `pigen_const_expr_intern_width_maximum()` over both operand widths and a compatible expected width;
- ignore a narrower compatible expectation automatically because symbolic maximum retains the inferred width;
- reject an expected type from the other integer family;
- make shifts use only the left integer width/family and require an unsigned-integer count;
- apply the byte operator matrix from Step 2;
- fill conversion records first, then construct the effective operation from their target identities;
- leave result records untouched on failure.

For conditional expressions, allow same-family Pigen integer alternatives to use the same common-width policy and byte alternatives only when both are byte. The condition may be any type already admitted for logical use. Preserve the current exact-identity SystemVerilog conditional behavior; do not invent incomplete SV mixed-width merging.

- [ ] **Step 6: Adapt existing identity-only callers**

Update `src/data_type.c`'s internal unsized-integer width arithmetic and `src/expression_resolve.c` to pass no expected type and receive resolution records. Before expression conversion nodes exist, callers must accept only identity conversions and pass `resolution.operation` to the existing semantic constructors:

```c
if (resolution.left_conversion.kind != PIGEN_CONVERSION_IDENTITY ||
	resolution.right_conversion.kind != PIGEN_CONVERSION_IDENTITY)
	return INVALID_ID(pigen_expr_id);
return pigen_expr_add_binary(resolver->model, resolution.operation,
	left, right, syntax->location.source_span);
```

This is an explicit incomplete boundary, not a fallback: target Pigen type syntax is still unavailable, while current accepted structured expressions retain their behavior. The next expression-integration slice will replace this identity gate with semantic conversion-node construction.

Update existing semantic and expression-resolution tests to inspect `.operation` and identity conversions. Do not weaken their existing assertions.

- [ ] **Step 7: Run focused verification**

Run: `make semantic-test predicate-test expression-resolve-test expression-use-test resolve-test`

Expected: all five targets print `PASS` and exit zero.

- [ ] **Step 8: Update the tracked architecture status**

In `PLAN.md`, record that the data-type owner now has semantic-only Pigen integer/byte identities, symbolic common-width algebra, conversion decisions, and type-family-owned operation resolutions. Keep the phase-2 data-type and expression-integration checkboxes unchecked because target spelling, conversion nodes, contextual resolver plumbing, and lowering remain incomplete.

Compact the matching paragraphs in `agent_notes/COMPILER_ARCHITECTURE.md` and `agent_notes/SEMANTIC_INVARIANTS.md`; preserve the approved policy and explicitly name the identity-only expression boundary.

- [ ] **Step 9: Run full verification**

Run: `make verify`

Expected: every compile, unit, smoke, Icarus, and Verilator target exits zero. In particular, `signed-widen-test` continues to pass through the unchanged production compiler, and ordinary SystemVerilog compatibility behavior is unchanged.

- [ ] **Step 10: Commit operation policy**

```bash
git add include/pigen/operation.h include/pigen/data_type.h src/data_type.c src/expression_resolve.c tests/semantic_test.c tests/expression_resolve_test.c PLAN.md agent_notes/COMPILER_ARCHITECTURE.md agent_notes/SEMANTIC_INVARIANTS.md
git commit -m "resolve operations through data type policy"
```

## Final review gate

- [ ] Confirm no file outside `src/data_type.c` enumerates Pigen integer or byte constructors.
- [ ] Confirm the new source spellings remain unavailable until the target declaration plan.
- [ ] Confirm non-identity decisions are structural records, not emitted strings or textual casts.
- [ ] Confirm expression resolution has one conspicuous identity-only boundary and no shadow semantic path.
- [ ] Confirm `git diff --check` and `make verify` pass from a clean worktree.
