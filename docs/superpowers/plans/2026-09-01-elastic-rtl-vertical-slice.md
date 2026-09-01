# Elastic RTL Vertical Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a complete, testable structured path from the existing semantic model through ready-cycle validation, elastic RTL IR, ordered compilation output, and terminal SystemVerilog emission for one module with direct guarded transfers.

**Architecture:** Arena-based RTL records carry only resolved hardware identities and provenance. Narrow adapters lower canonical semantic types, expressions, realizations, and transfers without inspecting source text; a separate ordered output model interleaves opaque written SystemVerilog spans with structured RTL references. This slice remains quarantined from `./pigen` until later plans migrate every retained production subsystem and delete the prototype in one clean switch.

**Tech Stack:** C17, existing source/preprocessor/syntax/semantic arenas, descriptor-owned transfer realizations, Make, Icarus Verilog, and structural shell checks.

**Spec:** `docs/superpowers/specs/2026-09-01-elastic-rtl-vertical-slice-design.md`

## Global Constraints

- Work test-first: add one discriminating failing test, run its narrow target and observe the intended failure, implement the owning behavior, then rerun it.
- Use lowercase snake_case for C identifiers; use uppercase snake case only for preprocessor definitions and enum constants.
- RTL IR contains no source spelling, opaque source text, or source transfer-type identity.
- Lowering may switch on `pigen_transfer_realization`; it may not switch on `pigen_transfer_type`.
- The emitter consumes RTL and output-layout records only. It performs no scope lookup, type resolution, width inference, ownership analysis, or text search.
- Opaque SystemVerilog spans are copied only by the terminal output layer.
- Do not connect this partial path to `src/pigen.c`, add a validator mode, select compilers per file, or feed prototype-generated text into it.
- Every multi-arena constructor restores all touched counts on failure.
- Commit after every green task. Before each commit run `git diff --check` and inspect `git status --short`; stage only the named files.

---

### Task 1: Establish distinct RTL identities and the arena model

**Files:**

- Modify: `include/pigen/ids.h`
- Create: `include/pigen/rtl.h`
- Create: `src/rtl.c`
- Create: `tests/rtl_test.c`
- Modify: `Makefile`

**Interfaces:**

- Consumes: `pigen_source_span`, semantic operation enums, signedness, and state domain.
- Produces: `pigen_rtl_model`, distinct RTL identities, checked accessors, and `pigen_free_rtl_model()`.

- [ ] **Step 1: Write the failing empty-model test**

```c
#include <assert.h>
#include "pigen/rtl.h"

int main(void)
{
	pigen_rtl_model model = {0};
	assert(!pigen_rtl_module_get(&model, (pigen_rtl_module_id){0}));
	assert(!pigen_rtl_object_get(&model, (pigen_rtl_object_id){0}));
	assert(!pigen_rtl_expr_get(&model, (pigen_rtl_expr_id){0}));
	pigen_free_rtl_model(&model);
	return 0;
}
```

Add `rtl-test` to `Makefile`, compiling `tests/rtl_test.c src/rtl.c src/util.c`, and include it in `test`.

- [ ] **Step 2: Run `make rtl-test`**

Expected: compilation fails because the header and distinct identities are absent.

- [ ] **Step 3: Replace the unused generic identity**

Delete `pigen_rtl_id` and add:

```c
PIGEN_ID_TYPE(pigen_rtl_type_id);
PIGEN_ID_TYPE(pigen_rtl_expr_id);
PIGEN_ID_TYPE(pigen_rtl_object_id);
PIGEN_ID_TYPE(pigen_rtl_instance_id);
PIGEN_ID_TYPE(pigen_rtl_equation_id);
PIGEN_ID_TYPE(pigen_rtl_update_id);
PIGEN_ID_TYPE(pigen_rtl_process_id);
PIGEN_ID_TYPE(pigen_rtl_module_id);
```

Define `pigen_rtl_model` with pointer/count/capacity triples for each arena and no source-text pointer. Give every public enum an invalid zero member.

- [ ] **Step 4: Implement bounds-checked accessors and destruction**

Each accessor returns null for a null model or out-of-range identity. Free every arena and reset the complete model to zero.

- [ ] **Step 5: Verify and commit**

```sh
make rtl-test
git diff --check
git status --short
git add include/pigen/ids.h include/pigen/rtl.h src/rtl.c tests/rtl_test.c Makefile
git commit -m "establish elastic RTL identities"
```

---

### Task 2: Add canonical RTL types and expressions

**Files:**

- Modify: `include/pigen/rtl.h`
- Modify: `src/rtl.c`
- Modify: `tests/rtl_test.c`

**Interfaces:**

- Consumes: Task 1 identities and existing operation/conversion records.
- Produces: interned `pigen_rtl_type`, immutable `pigen_rtl_expr`, child ranges, constructors, and accessors.

- [ ] **Step 1: Write failing interning and child-order tests**

```c
pigen_rtl_type_id first = pigen_rtl_type_intern(&model, type);
pigen_rtl_type_id second = pigen_rtl_type_intern(&model, type);
pigen_rtl_expr_id children[] = {left, right};
pigen_rtl_expr_id concat = pigen_rtl_expr_add_concatenation(
	&model, first, children, 2, span);

assert(first.index == second.index);
assert(pigen_rtl_expr_children(&model, concat)[0].index == left.index);
assert(pigen_rtl_expr_children(&model, concat)[1].index == right.index);
```

Force a concatenation with an invalid child and assert expression and child counts remain unchanged.

- [ ] **Step 2: Run `make rtl-test`**

Expected: compilation fails on the new APIs.

- [ ] **Step 3: Implement the expression algebra**

```c
typedef enum {
	PIGEN_RTL_EXPR_INVALID,
	PIGEN_RTL_EXPR_INTEGER,
	PIGEN_RTL_EXPR_BITS,
	PIGEN_RTL_EXPR_OBJECT,
	PIGEN_RTL_EXPR_UNARY,
	PIGEN_RTL_EXPR_BINARY,
	PIGEN_RTL_EXPR_CONDITIONAL,
	PIGEN_RTL_EXPR_CONVERSION,
	PIGEN_RTL_EXPR_INDEX,
	PIGEN_RTL_EXPR_SELECT,
	PIGEN_RTL_EXPR_CONCATENATION
} pigen_rtl_expr_kind;
```

Every expression stores kind, type, and span. Store operation and conversion records, not operator text. Store sequence children in one append-only arena. Validate all identities before appending.

- [ ] **Step 4: Cover every node and invalid reference**

Construct literals, object references, unary, binary, conditional, conversion, index, select, and concatenation nodes. Assert spans and explicit conversions survive. Invalid types, operands, objects, selectors, and children append nothing.

- [ ] **Step 5: Verify and commit**

```sh
make rtl-test
git diff --check
git add include/pigen/rtl.h src/rtl.c tests/rtl_test.c
git commit -m "represent RTL types and expressions"
```

---

### Task 3: Represent modules, hardware objects, equations, and processes

**Files:**

- Modify: `include/pigen/rtl.h`
- Modify: `src/rtl.c`
- Modify: `tests/rtl_test.c`

**Interfaces:**

- Consumes: Task 2 types and expressions.
- Produces: module-local objects, instances, equations, guarded updates, clocked processes, ordered child ranges, and semantic provenance.

- [ ] **Step 1: Write a failing complete-module test**

Exercise these exact public interfaces:

```c
pigen_rtl_module_id pigen_rtl_module_add(pigen_rtl_model *model,
	pigen_module_id semantic_module, pigen_source_span span);
pigen_rtl_object_id pigen_rtl_object_add(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_rtl_object_kind kind,
	pigen_rtl_type_id type, pigen_rtl_direction direction,
	pigen_signal_id semantic_signal, pigen_source_span span);
pigen_rtl_process_id pigen_rtl_process_add(pigen_rtl_model *model,
	pigen_rtl_module_id module, pigen_rtl_expr_id clock,
	pigen_semantic_edge edge, const pigen_rtl_update_id *updates,
	size_t update_count, pigen_source_span span);
```

Build one input, internal object, equation, primitive instance, and process/update. Assert order and provenance.

- [ ] **Step 2: Run `make rtl-test`**

Expected: compilation fails on the module APIs.

- [ ] **Step 3: Implement transactional child ranges**

Add module-owned ranges for objects, instances, equations, and processes; instance-owned parameter/connection ranges; and process-owned update ranges. Validate module ownership. Semantic identities are provenance only; invalid ones are allowed for synthetic RTL.

- [ ] **Step 4: Test cross-module rejection**

Reject a foreign destination equation, invalid instance connection, and foreign update. Assert all arena counts and owner ranges remain unchanged.

- [ ] **Step 5: Verify and commit**

```sh
make rtl-test
git diff --check
git add include/pigen/rtl.h src/rtl.c tests/rtl_test.c
git commit -m "represent structured RTL modules"
```

---

### Task 4: Centralize collision-safe RTL names

**Files:**

- Modify: `include/pigen/ids.h`
- Modify: `include/pigen/rtl.h`
- Create: `include/pigen/rtl_name.h`
- Create: `src/rtl_name.c`
- Create: `tests/rtl_name_test.c`
- Modify: `Makefile`

**Interfaces:**

- Consumes: RTL module/object/instance identities and preferred source spans or internal roles.
- Produces: immutable final-name identities and `pigen_rtl_assign_names()`.

- [ ] **Step 1: Write failing collision tests**

Create source names `value`, `value_valid`, and `value__pigen_valid`, then request an internal valid name for `value`. Assert all names differ, repeated assignment is stable, and an independently built identical model produces byte-identical names.

- [ ] **Step 2: Run `make rtl-name-test`**

Expected: compilation fails because the name subsystem is absent.

- [ ] **Step 3: Implement one module-local allocator**

Add `pigen_rtl_name_id` to `ids.h` and expose:

```c
typedef enum {
	PIGEN_RTL_NAME_SOURCE,
	PIGEN_RTL_NAME_PAYLOAD,
	PIGEN_RTL_NAME_VALID,
	PIGEN_RTL_NAME_READY,
	PIGEN_RTL_NAME_INSTANCE,
	PIGEN_RTL_NAME_TEMPORARY
} pigen_rtl_name_role;

int pigen_rtl_assign_names(pigen_rtl_model *model,
	const pigen_source_manager *sources);
const char *pigen_rtl_name_get(const pigen_rtl_model *model,
	pigen_rtl_name_id name);
```

Source roles copy identifier spelling from provenance. Internal roles derive a deterministic stem and add numeric suffixes only on collision. No semantic consumer queries by name.

- [ ] **Step 4: Test atomic failure**

An invalid source span fails without publishing partial names. Synthetic internal names remain legal without a source span.

- [ ] **Step 5: Verify and commit**

```sh
make rtl-test rtl-name-test
git diff --check
git add include/pigen/ids.h include/pigen/rtl.h include/pigen/rtl_name.h src/rtl_name.c tests/rtl_name_test.c Makefile
git commit -m "centralize RTL name allocation"
```

---

### Task 5: Lower semantic types and expressions through owner APIs

**Files:**

- Create: `include/pigen/rtl_lower.h`
- Create: `src/rtl_lower.c`
- Create: `tests/rtl_lower_test.c`
- Modify: `Makefile`

**Interfaces:**

- Consumes: validated semantic model, canonical type/shape queries, semantic expressions, and RTL constructors.
- Produces: `pigen_rtl_lowering`, memoized mappings, `pigen_lower_rtl_type()`, and `pigen_lower_rtl_expression()`.

- [ ] **Step 1: Write failing type and conversion tests**

Resolve signed `int[8]`, unsigned `uint[12]`, `bit[16]`, widening arithmetic, an explicit cast, projection, and concatenation. Initialize:

```c
pigen_rtl_lowering lowering;
assert(pigen_rtl_lowering_init(&lowering, &semantics, &rtl));
pigen_rtl_expr_id lowered = pigen_lower_rtl_expression(
	&lowering, semantic_expression, &error);
```

Assert signedness, state domain, width identity, explicit conversion nodes, projection structure, and child order.

- [ ] **Step 2: Run `make rtl-lower-test`**

Expected: compilation fails because the adapter is absent.

- [ ] **Step 3: Implement type and constant lowering**

Call only the data-type owner queries for signedness, state domain, packed width, and dimensions. Lower constant DAGs once and memoize by identity. Preserve count versus range declarator dimensions.

- [ ] **Step 4: Implement semantic expression lowering**

Map every current semantic expression kind. Drop grouping only when the resulting node retains correct provenance; never drop conversions. Resolve symbols through an identity map populated by module lowering, never spelling.

- [ ] **Step 5: Test rollback, verify, and commit**

An unbound signal and invalid semantic expression append no RTL records.

```sh
make rtl-test rtl-name-test rtl-lower-test
git diff --check
git add include/pigen/rtl_lower.h src/rtl_lower.c tests/rtl_lower_test.c Makefile
git commit -m "lower typed expressions into RTL"
```

---

### Task 6: Lower signal realizations without enumerating transfer types

**Files:**

- Modify: `include/pigen/rtl_lower.h`
- Modify: `src/rtl_lower.c`
- Modify: `tests/rtl_lower_test.c`

**Interfaces:**

- Consumes: semantic signals, transfer descriptors, realization descriptors, and RTL module constructors.
- Produces: `pigen_lower_rtl_module_declarations()` and stable payload/valid/ready endpoint mappings.

- [ ] **Step 1: Write a failing realization matrix**

Resolve abstract input, `wire`, `reg`, `logic`, `buf`, `port`, `fifo[4]`, and `skid`. Assert:

| Realization | RTL structure |
| --- | --- |
| boundary | payload/valid/ready ports |
| combinational net | payload net and constant controls |
| procedural variable | payload variable and constant controls |
| elastic slot | `pigen_buf` instance |
| pulse register | `pigen_port` instance |
| parameterized queue | `pigen_fifo` with semantic depth |
| skid queue | `pigen_skid` instance |

- [ ] **Step 2: Run `make rtl-lower-test`**

Expected: the declaration-lowering API is missing.

- [ ] **Step 3: Implement realization-owned lowering**

Read the source descriptor only to obtain `realization`; dispatch through a private table indexed by `pigen_transfer_realization`. Query capacity, ready dependency, occupancy, and reset from the realization descriptor.

```c
typedef struct {
	pigen_rtl_object_id payload;
	pigen_rtl_expr_id valid;
	pigen_rtl_expr_id ready;
	pigen_rtl_object_id input_payload;
	pigen_rtl_object_id input_valid;
	pigen_rtl_object_id input_ready;
} pigen_rtl_signal_endpoints;
```

Constant controls use invalid object identities and valid constant-expression identities.

- [ ] **Step 4: Prove the owner boundary**

```sh
rg -n "PIGEN_TRANSFER_TYPE_(ABSTRACT|WIRE|REG|LOGIC|BUF|PORT|FIFO|SKID)" src/rtl_lower.c
```

Expected: no matches.

- [ ] **Step 5: Verify and commit**

```sh
make transfer-type-test rtl-lower-test
git diff --check
git add include/pigen/rtl_lower.h src/rtl_lower.c tests/rtl_lower_test.c
git commit -m "lower transfer realizations into RTL"
```

---

### Task 7: Validate the whole-unit ready-dependency graph

**Files:**

- Create: `include/pigen/ready_graph.h`
- Create: `src/ready_graph.c`
- Create: `tests/ready_graph_test.c`
- Modify: `Makefile`

**Interfaces:**

- Consumes: semantic signals/transfers/incidence and realization ready laws.
- Produces: `pigen_validate_ready_dependencies()` and source-located semantic errors.

- [ ] **Step 1: Write failing graph tests**

Construct `a -> b -> c`, `a -> b -> c -> a`, `a -> a`, and a cycle crossing a FIFO:

```c
assert(pigen_validate_ready_dependencies(&acyclic, NULL));
assert(!pigen_validate_ready_dependencies(&cyclic, &error));
assert(!strcmp(error.message, "combinational ready-dependency cycle"));
assert(pigen_validate_ready_dependencies(&fifo_broken, NULL));
```

Assert the error span is the transfer closing the cycle.

- [ ] **Step 2: Run `make ready-graph-test`**

Expected: compilation fails because the validator is absent.

- [ ] **Step 3: Build the graph from identities**

Create one vertex per downstream-ready realization. Add edges from deduplicated transfer incidence. Constant, external, and occupancy dependencies terminate paths. Never render or compare names.

- [ ] **Step 4: Implement deterministic Tarjan validation**

Reject a strongly connected component with multiple vertices or a self-edge. Choose the lowest source-ordered closing transfer for diagnostics. Free temporary arrays on every exit.

- [ ] **Step 5: Test permutations and duplicate incidence**

Reorder declaration insertion while retaining transfer order and assert the same diagnostic. Repeated projections of one source create no duplicate vertex or false cycle.

- [ ] **Step 6: Verify and commit**

```sh
make ready-graph-test resolve-test
git diff --check
git add include/pigen/ready_graph.h src/ready_graph.c tests/ready_graph_test.c Makefile
git commit -m "validate ready dependency cycles"
```

---

### Task 8: Lower direct transfers from one fire identity

**Files:**

- Modify: `include/pigen/rtl_lower.h`
- Modify: `src/rtl_lower.c`
- Modify: `tests/rtl_lower_test.c`

**Interfaces:**

- Consumes: transfer incidence, predicate atoms, endpoint mappings, and expression lowering.
- Produces: equations and guarded updates from `pigen_lower_rtl_transfers()`, plus the complete `pigen_lower_rtl_module()` adapter used by the composer.

- [ ] **Step 1: Write a failing atomic join test**

Resolve guarded `out <= left + right`. Assert one fire expression contains the guard, destination ready, and each distinct source valid exactly once. Assert all source-ready equations and the destination update reference the same fire identity.

- [ ] **Step 2: Run `make rtl-lower-test`**

Expected: transfer lowering is missing.

- [ ] **Step 3: Implement fire construction**

Lower predicate atoms in canonical order. Add distinct consumer-valid and producer-ready dependencies from `pigen_transfer_signal_uses()`. Build one conjunction and store it on every update. Constants and statics contribute descriptor constants only.

- [ ] **Step 4: Lower lvalues and static destinations**

Recursively lower projections and concatenations. Buffered destinations remain whole; static destinations may retain index/select nodes. Preserve concatenation order and the semantic assignment conversion above the complete RHS.

- [ ] **Step 5: Test deduplication and process ownership**

Cover repeated projections, a static-only assignment, and mutually exclusive guarded producers. Assert one source-ready equation, ordered static updates, and one RTL process per semantic process.

Implement `pigen_lower_rtl_module()` as the narrow composition boundary: add
the RTL module, lower its declarations, bind semantic signals to endpoints,
lower its processes and transfers, and return the RTL module identity. A
failure restores the complete RTL model and lowering-map counts to their entry
values.

- [ ] **Step 6: Verify and commit**

```sh
make rtl-lower-test ready-graph-test resolve-test
git diff --check
git add include/pigen/rtl_lower.h src/rtl_lower.c tests/rtl_lower_test.c
git commit -m "lower atomic transfers into RTL"
```

---

### Task 9: Represent ordered opaque and structured output

**Files:**

- Create: `include/pigen/output.h`
- Create: `src/output.c`
- Create: `tests/output_test.c`
- Modify: `Makefile`

**Interfaces:**

- Consumes: immutable source spans, the syntax tree, semantic-to-RTL mappings, and RTL identities.
- Produces: `pigen_output_model`, `pigen_build_output_model()`, compilation/module layouts, validation, and destruction.

- [ ] **Step 1: Write a failing exact-coverage test**

Build opaque/module/opaque compilation layout and opaque/RTL/opaque nested module layout. Assert validation accepts exact monotonic coverage and rejects gaps, overlaps, reversed spans, wrong source ids, and invalid RTL references.

- [ ] **Step 2: Run `make output-model-test`**

Expected: the model is absent.

- [ ] **Step 3: Implement layout-only records**

```c
typedef enum {
	PIGEN_OUTPUT_OPAQUE,
	PIGEN_OUTPUT_MODULE,
	PIGEN_OUTPUT_RTL_OBJECT,
	PIGEN_OUTPUT_RTL_INSTANCE,
	PIGEN_OUTPUT_RTL_EQUATION,
	PIGEN_OUTPUT_RTL_PROCESS
} pigen_output_item_kind;
```

Opaque items contain only source spans. Structured items contain only matching identities. Module items refer to nested layout identities. No API returns or searches opaque bytes.

Implement `pigen_build_output_model()` by walking syntax child order. Opaque
syntax nodes contribute their written source spans. Recognized declarations
and processes contribute the RTL identities recorded in the lowering map.
Punctuation and trivia between child extents become opaque spans, so the
result covers each written byte once without interpreting it. Reject a
structured syntax node which has no lowered identity at that node's original
span.

- [ ] **Step 4: Test rollback and destruction**

A failed nested append restores layout/item/child counts. Destruction leaves a zero model.

- [ ] **Step 5: Verify and commit**

```sh
make output-model-test rtl-test
git diff --check
git add include/pigen/output.h src/output.c tests/output_test.c Makefile
git commit -m "represent ordered compiler output"
```

---

### Task 10: Emit SystemVerilog from output and RTL records

**Files:**

- Create: `include/pigen/sv_emit.h`
- Create: `src/sv_emit.c`
- Create: `tests/sv_emit_test.c`
- Modify: `Makefile`

**Interfaces:**

- Consumes: sources for opaque copying, validated output model, and named RTL model.
- Produces: `pigen_emit_systemverilog()` into `pigen_string`.

- [ ] **Step 1: Write a failing mixed-output golden test**

Build leading/trailing opaque comments and one RTL module containing a signed conversion, concatenation, primitive parameter, equation, and guarded update:

```c
pigen_string output = {0};
assert(pigen_emit_systemverilog(&sources, &layout, &rtl, &output));
assert(!strcmp(output.data, expected));
```

- [ ] **Step 2: Run `make sv-emit-test`**

Expected: the emitter is absent.

- [ ] **Step 3: Implement precedence-aware expressions**

Assign fixed precedence to every RTL expression kind. Parenthesize only when structure requires it. Render operation enums from one private table and explicit conversions from RTL types only.

- [ ] **Step 4: Implement structured and opaque rendering**

Render named ports, declarations, instances, equations, and processes in layout order. Opaque items copy the checked half-open source span exactly. Never tokenize or search text.

- [ ] **Step 5: Test malformed inputs and structural purity**

Malformed coverage, unnamed objects, invalid children, and out-of-bounds spans fail without partial output.

```sh
rg -n "strstr|strchr|pigen_symbol_lookup|pigen_data_type_|pigen_transfer_type_" src/sv_emit.c
```

Expected: no matches.

- [ ] **Step 6: Verify and commit**

```sh
make sv-emit-test output-model-test rtl-test
git diff --check
git add include/pigen/sv_emit.h src/sv_emit.c tests/sv_emit_test.c Makefile
git commit -m "emit SystemVerilog from structured RTL"
```

---

### Task 11: Compose the quarantined vertical slice

**Files:**

- Create: `include/pigen/compile.h`
- Create: `src/compile.c`
- Create: `tests/compile_test.c`
- Create: `tests/vertical_slice.pigen`
- Create: `tests/vertical_slice_tb.sv`
- Modify: `Makefile`

**Interfaces:**

- Consumes: preprocess, syntax, resolution, ready validation, lowering/naming, output layout, and emission.
- Produces: testable `pigen_compile_source()` and no executable integration.

- [ ] **Step 1: Write the failing end-to-end test**

Use a data-first module with abstract inputs, `buf`, `port`, `fifo[4]`, `skid`, one clocked process, guarded direct transfers, signed arithmetic, projection, and concatenation:

```c
pigen_compile_options options = {.maximum_generated_bits = 4096};
pigen_compile_result result = {0};
pigen_compile_error error = {0};

assert(pigen_compile_source(&sources, source, &options, &result, &error));
assert(result.systemverilog.data);
pigen_free_compile_result(&result);
```

Assert opaque comments survive exactly and no marker comment appears.

- [ ] **Step 2: Run `make vertical-slice-test`**

Expected: the composer is absent.

- [ ] **Step 3: Implement explicit phase sequencing**

The function calls, in order:

```c
pigen_preprocess(...);
pigen_parse_syntax(...);
pigen_resolve_semantics(...);
pigen_validate_ready_dependencies(...);
pigen_lower_rtl_module(...);
pigen_rtl_assign_names(...);
pigen_build_output_model(...);
pigen_emit_systemverilog(...);
```

Map native errors into one compile error without losing origin/span. Free completed models in reverse order on failure. Do not call prototype lexing or rewrite APIs.

- [ ] **Step 4: Compile and simulate**

Write only `/tmp/pigen-vertical-slice.sv`, then run:

```sh
iverilog -g2012 -o /tmp/pigen-vertical-slice-vvp \
	rtl/pigen_primitives.sv /tmp/pigen-vertical-slice.sv \
	tests/vertical_slice_tb.sv
vvp /tmp/pigen-vertical-slice-vvp
```

The testbench holds ready low, checks payload stability, releases backpressure, exercises simultaneous FIFO push/pop, and observes one guarded transfer.

- [ ] **Step 5: Test phase errors**

Use in-memory sources for syntax error, type mismatch, cross-domain transfer, ready cycle, and unsupported Pigen-dependent opaque form. Assert exact source id/span and no output.

- [ ] **Step 6: Prove quarantine**

```sh
rg -n "pigen_compile_source|pigen_lower_rtl|pigen_emit_systemverilog" src/pigen.c src/blocks.c src/pipeline.c src/fsm.c
```

Expected: no matches.

- [ ] **Step 7: Verify and commit**

```sh
make vertical-slice-test rtl-test rtl-name-test rtl-lower-test ready-graph-test output-model-test sv-emit-test resolve-test
git diff --check
git status --short
git add include/pigen/compile.h src/compile.c tests/compile_test.c tests/vertical_slice.pigen tests/vertical_slice_tb.sv Makefile
git commit -m "compose structured compiler slice"
```

---

### Task 12: Verify and update the durable boundary

**Files:**

- Modify: `PLAN.md`
- Modify: `agent_notes/COMPILER_ARCHITECTURE.md`
- Modify: `agent_notes/SEMANTIC_INVARIANTS.md`

**Interfaces:**

- Consumes: Tasks 1-11 and their evidence.
- Produces: accurate status and the next migration entry point.

- [ ] **Step 1: Run the clean structured suite**

```sh
make clean
make source-test preprocess-test transfer-type-test syntax-model-test integer-test semantic-test predicate-test expression-resolve-test expression-use-test resolve-test rtl-test rtl-name-test rtl-lower-test ready-graph-test output-model-test sv-emit-test vertical-slice-test
```

Expected: every target passes with `-Werror`.

- [ ] **Step 2: Run unchanged production verification**

```sh
make verify
```

Expected: the quarantined prototype remains behaviorally unchanged. Inspect any generated tracked examples before touching them; preserve unrelated user edits.

Expected repository effect: no tracked output changes, because this plan does
not alter the production executable. Treat any difference as evidence to
investigate, not generated churn to discard casually.

- [ ] **Step 3: Run side-channel checks**

```sh
rg -n "strstr|marker|rewrite|generated.*lookup" src/rtl.c src/rtl_lower.c src/ready_graph.c src/output.c src/sv_emit.c src/compile.c
rg -n "PIGEN_TRANSFER_TYPE_(ABSTRACT|WIRE|REG|LOGIC|BUF|PORT|FIFO|SKID)" src/rtl_lower.c
rg -n "pigen_compile_source|pigen_lower_rtl|pigen_emit_systemverilog" src/pigen.c
```

Expected: no matches.

- [ ] **Step 4: Update status precisely**

In `PLAN.md`, mark only RTL IR definition and the tested narrow adapter sub-slice complete. Leave production connection, prototype deletion, full core behavior, pipelines, FSMs, fabrics, and backend cutover unchecked.

Record in `COMPILER_ARCHITECTURE.md` that direct-transfer RTL is simulated but unlinked, and that retained core semantics are next. Add only implemented durable laws to `SEMANTIC_INVARIANTS.md`; remove statements describing implemented records as wholly future work.

- [ ] **Step 5: Review and commit**

```sh
git diff --check
git status --short
git diff -- PLAN.md agent_notes/COMPILER_ARCHITECTURE.md agent_notes/SEMANTIC_INVARIANTS.md
git add PLAN.md agent_notes/COMPILER_ARCHITECTURE.md agent_notes/SEMANTIC_INVARIANTS.md
git commit -m "record elastic RTL slice boundary"
```
