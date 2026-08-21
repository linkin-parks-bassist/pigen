# Data-type algebra foundation implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make expression and predicate consumers depend on one data-type
semantic interface rather than enumerating primitive constructors.

**Architecture:** Add a focused compile-time data-type module which resolves
aliases and owns integral capability and current unary, binary, and conditional
result typing. Expression resolution and predicates consume only this API. The
slice preserves the currently accepted operator semantics; it creates the
boundary through which later `int`, `uint`, `byte`, and fixed-point semantics
will be added.

**Tech Stack:** C17, existing arena/identity model, Make, assert-based unit tests.

**Spec:** `agent_notes/COMPILER_ARCHITECTURE.md`, section “Global shape, local
knowledge”.

## Global constraints

- No runtime registry, callbacks, plugins, fallback path, or parallel type
  system.
- Concrete primitive enumeration is owned by the data-type subsystem.
- Typedef aliases are transparent to capability queries but retain their
  canonical source type identity where current result rules preserve an
  operand type.
- No new primitive or new language-visible operator semantics are introduced
  in this slice.
- Ordinary SystemVerilog compatibility remains unchanged.

---

### Task 1: Prove the consumer boundary with alias expressions

**Files:**
- Modify: `tests/expression_resolve_test.c`

**Interfaces:**
- Consumes: existing typedef symbols, named semantic types, signal expressions,
  and `pigen_resolve_expression`.
- Produces: a failing behavioral case showing that arithmetic and comparison
  consumers must recognize an integral type through a typedef identity.

- [x] **Step 1: Extend the expression fixture**

Append `word_t aliased`, `aliased + aliased`, and
`aliased == aliased` to the fixture. Declare `word_t` as a typedef of the
model-owned integer type, construct its named type, and declare `aliased` as a
signal of that named type.

- [x] **Step 2: Assert intended results**

Assert that `aliased + aliased` resolves to a binary expression retaining the
named type, while `aliased == aliased` resolves to the model-owned boolean
type.

- [x] **Step 3: Run the focused test and observe the architectural failure**

Run: `make expression-resolve-test`

Expected: FAIL because `supported_integral_type` rejects
`PIGEN_TYPE_NAMED` without consulting the aliased type.

### Task 2: Establish and consume the data-type semantic API

**Files:**
- Create: `src/data_type.c`
- Modify: `include/pigen/semantic.h`
- Modify: `src/expression_resolve.c`
- Modify: `src/predicate.c`
- Modify: `Makefile`
- Test: `tests/expression_resolve_test.c`
- Test: `tests/semantic_test.c`

**Interfaces:**
- Consumes: `pigen_semantic_model`, canonical `pigen_type_id`, typedef symbol
  bindings, `pigen_unary_operator`, and `pigen_binary_operator`.
- Produces:
  `int pigen_data_type_is_integral(const pigen_semantic_model *, pigen_type_id)`,
  `pigen_type_id pigen_data_type_unary_result(pigen_semantic_model *,
  pigen_unary_operator, pigen_type_id)`,
  `pigen_type_id pigen_data_type_binary_result(pigen_semantic_model *,
  pigen_binary_operator, pigen_type_id, pigen_type_id)`, and
  `pigen_type_id pigen_data_type_conditional_result(pigen_semantic_model *,
  pigen_type_id, pigen_type_id, pigen_type_id)`.

- [x] **Step 1: Add direct semantic API assertions**

In `tests/semantic_test.c`, construct a typedef-wrapped integer type and assert
that the capability query accepts it, arithmetic preserves identical operand
identity, comparisons produce the boolean result type, and a non-integral or
invalid type is rejected.

- [x] **Step 2: Run the semantic test and observe the missing API**

Run: `make semantic-test`

Expected: compilation failure because the four data-type semantic functions do
not yet exist.

- [x] **Step 3: Implement alias-aware centralized rules**

In `src/data_type.c`, follow typedef bindings with an arena-count cycle bound.
Keep all concrete constructor checks inside this file. Implement the currently
accepted rules: logical/reduction unary operators produce boolean; other unary
operators preserve the operand type; comparisons and logical binary operators
produce boolean; other binary operators require identical operand identities
and preserve that identity; conditional expressions require an integral
condition and identical alternative identities.

- [x] **Step 4: Replace consumer-local rules**

Delete `supported_integral_type`, `unary_boolean_result`, and
`binary_boolean_result` from `src/expression_resolve.c`. Resolve result types
through the new API. Replace `predicate.c`'s concrete constructor list with
`pigen_data_type_is_integral`.

- [x] **Step 5: Link the module and run focused tests**

Add `src/data_type.c` to every replacement-middle test target which links
`src/semantic.c`.

Run:
`make semantic-test predicate-test expression-resolve-test expression-use-test resolve-test`

Expected: all five tests pass.

- [x] **Step 6: Commit the working vertical slice**

Commit message: `centralize data type operation semantics`

### Task 3: Audit the boundary and update durable artifacts

**Files:**
- Modify: `PLAN.md`
- Modify: `README.md`
- Modify: `agent_notes/COMPILER_ARCHITECTURE.md`
- Delete or compact: `agent_notes/data_type_algebra_plan.md`

**Interfaces:**
- Consumes: the completed data-type query boundary and repository doctrine.
- Produces: accurate status and an explicit record of remaining scattered
  type concerns, without claiming the full algebra complete.

- [ ] **Step 1: Search for remaining consumer enumeration**

Run:
`rg -n 'PIGEN_TYPE_(LOGIC|BIT|INTEGER|NAMED)' src include --glob '*.[ch]'`

Classify every remaining occurrence as owned structural implementation,
syntax/resolution boundary mapping, or unresolved scattering. Record the latter
in `PLAN.md` and the architecture note.

- [ ] **Step 2: Update status documents**

State that integral capability and current operator-result typing are
centralized, while packed layout/state-domain logic, primitive spelling,
conversion insertion, contextual sizing, and lowering still remain to be
consolidated.

- [ ] **Step 3: Run verification**

Run: `git diff --check`, the forbidden-terminology audit, then `make verify`.

Expected: clean formatting, no discarded terminology, and successful full
verification.

- [ ] **Step 4: Commit the documentation checkpoint**

Commit message: `record data type algebra cutover`
