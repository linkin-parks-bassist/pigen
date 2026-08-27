# Source-Visible Data-First Declarations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make data-first signal declarations authoritative in the shared structured frontend, remove Pigen `byte`, and resolve every recognized declaration through one syntax and semantic path without changing the production compiler path.

**Architecture:** The shared type parser owns structural data-type syntax, the transfer catalogue owns written transfer spellings and parameter forms, the data-type owner supplies unqualified-transfer policy, and one declaration resolver combines those independent decisions into canonical semantic signals. Ordinary supported SystemVerilog enters through a narrow surface adapter but produces the same declaration nodes; unsupported SystemVerilog remains one transactional opaque region. The production textual prototype remains quarantined until vertical RTL lowering.

**Tech Stack:** C17, the existing arena-based lexer/preprocessor/syntax/semantic infrastructure, Make targets, shell removal checks, and the complete Icarus Verilog behavioral suite.

**Spec:** `docs/superpowers/specs/2026-08-27-data-first-declarations-design.md`

## Global Constraints

- Work test-first: add the focused failing assertion, run its narrow target and observe the intended failure, implement only the owning behavior, then rerun it.
- Do not add a legacy transfer-first parser, Pigen `byte` alias, compatibility warning, feature flag, fallback, textual adapter, or second declaration representation.
- Do not connect this frontend to `src/pigen.c`, the prototype model, or either existing textual emitter.
- Keep concrete data-type family knowledge in `src/data_type.c` and written transfer parameter knowledge in `src/transfer_type.c`.
- One runtime declaration syntax kind and one declarator kind must serve explicit elastic transfers, explicit statics, inferred statics, and abstract inputs.
- Preserve ordinary-SystemVerilog behavior covered by the permanent production suite. Production `.pigen` fixtures remain transfer-first only because that parser is outside this slice.
- Commit after each green task. Before every commit run `git diff --check` and inspect `git status --short` so unrelated user work is never swept in.

---

### Task 1: Delete the Pigen `byte` data-type family cleanly

**Files:**

- Modify: `include/pigen/data_type.h`
- Modify: `src/data_type.c`
- Modify: `tests/semantic_test.c`
- Modify: `tests/expression_resolve_test.c`
- Modify: `tests/resolve_test.c`

- [ ] **Step 1: Write the owner-boundary tests which reject Pigen `byte`**

In `tests/semantic_test.c`, replace the positive Pigen-`byte` spelling assertions with assertions that:

```c
assert(pigen_data_type_spelling_domain(&model, byte_span) ==
	PIGEN_TYPE_SPELLING_SYSTEMVERILOG);
assert(pigen_data_type_from_spelling(&model, byte_span,
	PIGEN_SIGN_IMPLICIT, NULL, 0).index == PIGEN_INVALID_ID);
```

Retain ordinary eight-bit structural coverage by constructing `bit[8]` through the existing implicit/two-state packed representation. Rename local variables such as `byte_type` to `vector_8_type` or `octet_type` where they are not testing the deleted primitive.

- [ ] **Step 2: Run the semantic test and observe the old Pigen classification fail**

Run:

```sh
make semantic-test
```

Expected: the new spelling-domain or construction assertion fails because `byte` is still registered as a Pigen primitive.

- [ ] **Step 3: Remove the constructor, descriptor, public API, and family branches**

In `include/pigen/data_type.h`, delete:

```c
pigen_data_type_id pigen_data_type_byte(pigen_semantic_model *model);
```

In `src/data_type.c`, delete the private byte constructor and catalogue entry, byte interning invariant, Pigen spelling resolution, public constructor, `data_type_is_byte()` helper, and every byte-only branch in domain, signedness/numerical/state queries, conversion, unary, binary, conditional, concatenation, and projection logic.

Keep `byte` classified as a reserved SystemVerilog type spelling so it cannot be mistaken for a typedef, but return no semantic Pigen data type for it. Do not map it to `bit[8]`.

- [ ] **Step 4: Replace dependent tests with the surviving generic laws**

In `tests/semantic_test.c`, preserve tests for generic two-state packed eight-bit values, integer/vector explicit conversions, bitwise operations, selections, concatenations, and widths using `bit[8]` identities. Delete assertions whose only purpose was the special non-numerical byte family.

In `tests/expression_resolve_test.c`, replace valid `byte'(expression)` cases with `bit[8]'(expression)` and remove byte-family arithmetic rejection that no longer denotes a Pigen type.

In `tests/resolve_test.c`, replace `typedef byte byte_t` fixtures with an ordinary structural alias such as:

```systemverilog
typedef bit [7:0] octet_t;
```

Use neutral names throughout; no current test should imply that `byte` is Pigen neutral storage.

- [ ] **Step 5: Run the affected owner and expression tests**

Run:

```sh
make semantic-test expression-resolve-test resolve-test
```

Expected: all three targets pass, and `byte` is no longer constructible through the Pigen data-type API.

- [ ] **Step 6: Confirm code-level removal and commit**

Run:

```sh
rg -n "PIGEN_DATA_TYPE_BYTE|pigen_data_type_byte|data_type_is_byte" include src tests
git diff --check
```

Expected: the removal search prints nothing and the diff check succeeds.

Commit:

```sh
git add include/pigen/data_type.h src/data_type.c tests/semantic_test.c tests/expression_resolve_test.c tests/resolve_test.c
git commit -m "remove pigen byte data type"
```

---

### Task 2: Centralize unqualified-transfer policy and positive counts

**Files:**

- Modify: `include/pigen/data_type.h`
- Modify: `src/data_type.c`
- Modify: `include/pigen/semantic.h`
- Modify: `src/semantic.c`
- Modify: `src/type_resolve.c`
- Modify: `tests/semantic_test.c`
- Modify: `tests/expression_resolve_test.c`

- [ ] **Step 1: Write failing policy tests at the data-type owner**

Add this public enum and function declaration to the test's expected interface:

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

In `tests/semantic_test.c`, assert this matrix directly:

| Semantic data type | Input | Non-input |
| --- | --- | --- |
| signed Pigen integer | abstract | forbidden |
| unsigned Pigen integer | abstract | forbidden |
| `bit` | abstract | static |
| supported ordinary `logic` | static | static |
| alias of Pigen integer | abstract | forbidden |
| alias of `bit` | abstract | static |

Also assert invalid model/type inputs return `PIGEN_UNQUALIFIED_TRANSFER_FORBIDDEN`.

- [ ] **Step 2: Run the semantic test and observe the missing API fail to compile**

Run:

```sh
make semantic-test
```

Expected: compilation fails because the policy enum/function do not yet exist.

- [ ] **Step 3: Implement the policy entirely inside `data_type.c`**

Add the declarations to `include/pigen/data_type.h`. In `src/data_type.c`, unwrap aliases through the existing canonical underlying-type helper, then classify the private constructor once:

- signed/unsigned Pigen integers return abstract for inputs and forbidden otherwise;
- bare `bit` returns abstract for inputs and static otherwise;
- all supported ordinary SystemVerilog types return static;
- invalid identities return forbidden.

No parser or resolver may inspect a Pigen constructor to reproduce this matrix.

- [ ] **Step 4: Write failing shared positive-count tests**

Expose this semantic normalization primitive in `include/pigen/semantic.h`:

```c
pigen_const_expr_id pigen_const_expr_normalize_count(
	pigen_semantic_model *model,
	pigen_const_expr_id value);
```

Test that known positive integer and exact-integer constants return canonical unsized positive constants, symbolic constants remain symbolic, and zero, negative, over-wide, nonintegral, invalid-model, and invalid-identity inputs fail. Add one type-resolution assertion confirming `int[0]` still reports `type count must be a positive integer` after the extraction.

- [ ] **Step 5: Run the narrow tests and observe the missing normalizer fail**

Run:

```sh
make semantic-test expression-resolve-test
```

Expected: compilation fails because the shared count normalizer is not implemented.

- [ ] **Step 6: Move count normalization into the semantic constant owner**

Move the current `normalize_count()` logic from `src/type_resolve.c` to `src/semantic.c` as `pigen_const_expr_normalize_count()`. Preserve its exact behavior: known positive values become canonical unsized constants, a symbolic constant is retained, and every known nonpositive or nonintegral value is rejected. Replace the private call in `pigen_resolve_type()` with the public semantic helper.

- [ ] **Step 7: Run focused tests and commit the two owner APIs**

Run:

```sh
make semantic-test expression-resolve-test
git diff --check
```

Expected: both targets pass.

Commit:

```sh
git add include/pigen/data_type.h src/data_type.c include/pigen/semantic.h src/semantic.c src/type_resolve.c tests/semantic_test.c tests/expression_resolve_test.c
git commit -m "centralize declaration type policy"
```

---

### Task 3: Replace split declaration syntax with one data-first topology

**Files:**

- Modify: `include/pigen/syntax.h`
- Modify: `src/syntax.c`
- Modify: `tests/syntax_test.c`

- [ ] **Step 1: Replace syntax tests with the target source forms**

In `tests/syntax_test.c`, replace transfer-first fixtures with declarations including:

```systemverilog
module declarations #(
	parameter DEPTH = 8,
	parameter LANES = 2
) (
	input int[16] sample,
	input uint[8] opcode,
	input bit flag,
	input int[16] buf queued_sample,
	output int[16] buf result,
	output logic [7:0] ordinary
);
	typedef bit[8] packet_t;
	int[16] buf left, right;
	bit[8] wire mask;
	packet_t fifo[DEPTH] queue[LANES], alternate[3:0];
	bit logic tag;
endmodule
```

Assert every recognized declaration is `PIGEN_SYNTAX_SIGNAL_DECLARATION` and every child is `PIGEN_SYNTAX_SIGNAL_DECLARATOR`. Assert:

- `int[16]` has one data-type count argument;
- `fifo[DEPTH]` has one written transfer occurrence whose argument expression is `DEPTH`;
- `queue[LANES]` has one count-shaped declarator dimension;
- `alternate[3:0]` has one range-shaped declarator dimension;
- unqualified inputs have no written transfer occurrence;
- ANSI continuation declarators share their declaration while retaining order and independent shapes; and
- source locations for the written transfer token and depth expression are distinct and exact.

- [ ] **Step 2: Add transactional opacity and clean-break syntax tests**

Add fixtures proving:

- ordinary `byte ordinary_value;` remains one opaque region with no syntax/type/expression/shape arena leakage;
- an unsupported ordinary declaration with an initializer remains opaque and lossless;
- old `buf int[16] old_order;` fails at `buf` rather than producing a declaration;
- `int[16] buf value = 0;` fails at `=` because the Pigen prefix is unmistakable;
- `packet_t fifo queue;`, `packet_t fifo[] queue;`, `packet_t fifo[4][2] queue;`, and `bit[8] buf[2] value;` fail at the owning transfer syntax; and
- `bit[8] mystery value;` does not reinterpret `mystery` as a transfer type.

- [ ] **Step 3: Run the syntax test and observe the old topology fail**

Run:

```sh
make syntax-model-test
```

Expected: assertions fail or fixtures reject because the parser still expects transfer-first elastic syntax and emits static-specific nodes.

- [ ] **Step 4: Define the unified public syntax records**

In `include/pigen/syntax.h`, delete:

```c
PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATION
PIGEN_SYNTAX_STATIC_SIGNAL_DECLARATOR
```

and their union members. Add:

```c
typedef struct {
	pigen_transfer_type transfer_type;
	pigen_syntax_location location;
	pigen_syntax_expr_id argument;
} pigen_syntax_transfer_type_occurrence;
```

Make the sole declaration member exactly:

```c
struct {
	pigen_syntax_direction direction;
	pigen_syntax_type_id data_type;
	int has_transfer_type;
	pigen_syntax_transfer_type_occurrence transfer_type;
} signal_declaration;
```

`has_transfer_type == 0` is the only representation of omission. In that state, `transfer_type.argument` must be the invalid expression identity and consumers must not read the enum or location. A written no-argument transfer also has an invalid argument identity. Keep the existing sole `pigen_syntax_signal_declarator` record.

- [ ] **Step 5: Add a complete parser checkpoint**

In `src/syntax.c`, replace the process-specific rollback argument list with a private checkpoint record containing:

```c
typedef struct {
	size_t syntax_node_count;
	size_t expression_node_count;
	size_t expression_child_count;
	size_t type_node_count;
	size_t type_argument_count;
	size_t shape_dimension_count;
	pigen_syntax_error error;
} syntax_checkpoint;
```

Add `save_checkpoint()` and `restore_checkpoint()` helpers. Use them for clocked-process speculation and every declaration attempt. Restoration changes counts and the saved diagnostic only; allocated capacity remains reusable.

- [ ] **Step 6: Parse data type, transfer occurrence, then declarators**

Replace `nonstatic_transfer_type_at()`, the transfer-parameter-aware `parse_type()`, `parse_signal()`, `parse_value()`, and duplicate declarator appenders with these contained responsibilities:

1. `transfer_type_at()` queries `pigen_transfer_type_from_spelling()` and accepts every concrete descriptor, static or dynamic.
2. `parse_transfer_type_occurrence()` consumes one concrete transfer keyword and, according to `descriptor->parameter`, either no brackets or exactly one nonempty bracketed expression. It records the keyword location and argument expression identity.
3. `add_signal_declarator()` is the only declarator appender and owns all post-name shape dimensions.
4. `parse_data_first_declaration()` parses optional direction, calls `pigen_parse_type_prefix()`, optionally parses a following transfer occurrence, then parses one or more declarators into the unified node.
5. A small `parse_systemverilog_static_declaration()` adapter retains supported prefix forms such as `wire [7:0] value`, `output reg value`, and implicit packed ranges, but emits the same unified node. It must not accept dynamic transfer keywords in prefix position.

Run the ordinary static adapter before generic data-first recognition for unambiguous `wire`/`reg` prefixes. Treat `logic`/`bit` in first position as data types; a transfer occurrence appears only after the complete data type in target syntax.

For an absent transfer, initialize `has_transfer_type` to zero and the argument identity to invalid. For a present transfer, record only catalogue identities and descriptor-owned argument structure; do not infer semantics in the parser.

- [ ] **Step 7: Make declaration recognition transactional and decisive**

For module items and ANSI ports:

- save a checkpoint before attempting structured recognition;
- restore it and retain the whole region as opaque when a supported ordinary prefix is not fully recognized;
- after recognizing a Pigen data type or a post-type Pigen transfer keyword, report malformed trailing syntax at its first token instead of rolling back;
- group ANSI continuation items only when `declarator_candidate()` proves they contain a bare declarator and optional shapes; and
- reject a dynamic transfer in ANSI syntax without `input`/`output` at the direction site.

Delete transfer-first Pigen candidate scanning. Explicitly detect a leading dynamic transfer keyword and diagnose that a data type must precede the transfer, without parsing the old form.

- [ ] **Step 8: Run syntax tests and inspect topology-removal searches**

Run:

```sh
make syntax-model-test
rg -n "STATIC_SIGNAL|parse_signal|parse_value|add_static_signal_declarator|nonstatic_transfer_type_at" include/pigen/syntax.h src/syntax.c tests/syntax_test.c
git diff --check
```

Expected: the syntax test passes and the removal search prints nothing. A renamed unified helper such as `parse_signal_declaration()` is acceptable only if it does not match a superseded split path accidentally; prefer `parse_declaration()`.

- [ ] **Step 9: Commit the unified syntax cutover**

```sh
git add include/pigen/syntax.h src/syntax.c tests/syntax_test.c
git commit -m "parse data-first signal declarations"
```

---

### Task 4: Resolve every declaration through one semantic path

**Files:**

- Modify: `src/resolve.c`
- Modify: `tests/resolve_test.c`

- [ ] **Step 1: Write the positive declaration-resolution matrix**

Replace transfer-first sources in `tests/resolve_test.c` with data-first declarations and add one module which proves:

```systemverilog
typedef int[16] sample_t;
typedef bit flag_t;

module resolved (
	input int[16] abstract_integer,
	input uint[8] abstract_unsigned,
	input bit abstract_bit,
	input sample_t abstract_alias,
	input int[16] buf constrained_input,
	output bit static_bit,
	output logic [7:0] static_logic
);
	int[16] buf buffered;
	uint[24] skid response;
	bit port finished;
	bit[8] logic tag;
	sample_t fifo[8] pending[2];
	bit wire net_value;
	bit reg variable_value;
endmodule
```

Assert semantic data type, transfer type, direction, transfer argument, and shape independently for every signal. In particular, all four unqualified Pigen-domain inputs are `PIGEN_TRANSFER_TYPE_ABSTRACT`, FIFO depth evaluates to eight, and `pending` has a separate shape count of two.

Also include all concrete transfer types (`wire`, `reg`, `logic`, `buf`, `port`, `fifo`, `skid`) in the matrix.

- [ ] **Step 2: Write the failure and provenance matrix**

Add table-driven source failures for:

```systemverilog
int[8] missing_internal_transfer;
output int[8] missing_output_transfer;
inout int[8] buf dynamic_inout;
packet_t fifo[0] zero_depth;
packet_t fifo[-1] negative_depth;
packet_t fifo[runtime_signal] nonconstant_depth;
```

Assert the messages respectively identify forbidden omitted realization, unresolved output policy, dynamic `inout`, positive depth, and constant depth. Assert diagnostic spans point to the data type/policy site, written transfer occurrence, or exact depth expression as specified. Preserve duplicate-name and invalid-shape checks at the declarator name/dimension.

- [ ] **Step 3: Run the resolver test and observe the split resolver fail**

Run:

```sh
make resolve-test
```

Expected: the target declarations fail or resolve with the old static/nonstatic assumptions.

- [ ] **Step 4: Replace both declaration resolvers with one resolver**

In `src/resolve.c`:

- simplify `resolve_shape()` to accept only `PIGEN_SYNTAX_SIGNAL_DECLARATOR`;
- delete `semantic_transfer_type()`, `resolve_static_transfer_type()`, and `add_static_signal_declaration()`;
- rename the remaining declaration function to `add_signal_declaration()` and make it consume the unified syntax record; and
- dispatch only `PIGEN_SYNTAX_SIGNAL_DECLARATION` from `add_module()`.

Resolve the data type before deciding an omitted transfer. Never inspect its concrete constructor here.

- [ ] **Step 5: Resolve written and omitted transfers through their owners**

For a written transfer:

- query `pigen_transfer_type_descriptor_get()`;
- require a concrete descriptor;
- reject dynamic `inout` at the written transfer location;
- validate absence/presence of the syntax argument against `descriptor->parameter`; and
- carry the written transfer identity unchanged.

For an omitted transfer, call:

```c
pigen_data_type_unqualified_transfer_policy(model, data_type,
	direction == PIGEN_DIRECTION_INPUT)
```

Interpret only the returned property:

- abstract -> `PIGEN_TRANSFER_TYPE_ABSTRACT`;
- forbidden -> a declaration-policy diagnostic;
- static -> the existing SystemVerilog static default law.

Implement that default without primitive-family enumeration: input/inout defaults to `wire`; internal defaults to `logic`; output with an explicitly based structural type defaults to `logic`, while an implicit output type defaults to `wire`. Explicit written `wire`, `reg`, or `logic` always wins.

- [ ] **Step 6: Resolve descriptor parameters in the Pigen literal domain**

Add a private generic `resolve_transfer_argument()` which switches only on `descriptor->parameter`, never on `PIGEN_TRANSFER_TYPE_FIFO`.

For `PIGEN_TRANSFER_PARAMETER_NONE`, require the syntax argument to be invalid and return an invalid semantic expression identity as the valid absence value.

For `PIGEN_TRANSFER_PARAMETER_DEPTH`:

1. resolve with `pigen_resolve_constant_expression(..., PIGEN_LITERAL_DOMAIN_PIGEN, ...)`;
2. obtain its constant identity with `pigen_expr_constant()`;
3. validate it through `pigen_const_expr_normalize_count()`; and
4. retain the resolved semantic expression as the signal's transfer argument.

Use the argument expression's syntax location for nonconstant/nonpositive diagnostics. Symbolic positive-count expressions remain legal when they cannot yet be numerically evaluated, matching structural type counts.

- [ ] **Step 7: Declare all children uniformly**

For each unified declarator child:

- resolve and intern its shape;
- declare its symbol using the declarator name token;
- diagnose duplicates at that token;
- call `pigen_signal_add()` with the resolved data type, transfer identity, transfer argument, direction, and canonical shape; and
- advance by sibling identity.

There must be no static/dynamic duplicate loop.

- [ ] **Step 8: Run focused semantic resolution and removal checks**

Run:

```sh
make resolve-test expression-use-test
rg -n "STATIC_SIGNAL|add_static_signal_declaration|resolve_static_transfer_type|semantic_transfer_type" src/resolve.c include/pigen/syntax.h
git diff --check
```

Expected: both tests pass and the removal search prints nothing.

- [ ] **Step 9: Commit unified declaration resolution**

```sh
git add src/resolve.c tests/resolve_test.c
git commit -m "resolve unified signal declarations"
```

---

### Task 5: Harden compatibility, rollback, and diagnostics

**Files:**

- Modify: `tests/syntax_test.c`
- Modify: `tests/resolve_test.c`
- Modify if failures expose an owner bug: `src/syntax.c`
- Modify if failures expose an owner bug: `src/resolve.c`

- [ ] **Step 1: Add a table-driven ordinary-SystemVerilog preservation matrix**

Cover at least:

```systemverilog
wire scalar_wire;
wire [7:0] ranged_wire;
reg signed [15:0] variable;
logic [3:0] internal_logic;
bit internal_bit;
input wire [7:0] input_wire;
input logic [7:0] input_logic;
output reg [7:0] output_reg;
output logic [7:0] output_logic;
inout wire bidirectional;
```

Assert supported cases produce the unified syntax topology and the same semantic static transfer identity as before. Add unsupported ordinary declarations—ordinary `byte`, interface/modport forms, initializers, and one aggregate form outside the subset—and assert each remains a single opaque region with byte-for-byte source coverage and unchanged arena counts.

- [ ] **Step 2: Run focused tests and observe any compatibility gaps**

Run:

```sh
make syntax-model-test resolve-test
```

Expected: any failure identifies either an incomplete SystemVerilog surface adapter or a rollback leak, not a reason to add another syntax family.

- [ ] **Step 3: Add the full malformed-Pigen diagnostic matrix**

Cover missing/empty/repeated FIFO depth, non-FIFO transfer arguments, missing declarator, invalid continuation, initializer, dynamic `inout`, nonconstant/nonpositive depth, duplicate declarator, and invalid shape. For each, assert both message and smallest owning span.

Add an arena-count helper in `tests/syntax_test.c` which snapshots syntax nodes, expression nodes/children, type nodes/arguments, and shape dimensions around opaque declarations. This directly prevents partial speculative state from becoming a future hidden bug.

- [ ] **Step 4: Fix only the owning adapters and rerun**

Keep rollback mechanics in the parser checkpoint, semantic family rules in `data_type.c`, transfer parameter forms in the descriptor, and semantic diagnostics in the unified resolver. Do not patch compatibility by peeking into opaque source or by recognizing transfer-first Pigen.

Run:

```sh
make syntax-model-test resolve-test expression-use-test
git diff --check
```

Expected: all targets pass.

- [ ] **Step 5: Run structural clean-break searches**

Run:

```sh
rg -n "PIGEN_SYNTAX_STATIC_SIGNAL|static_signal_declaration|static_signal_declarator" include src tests
rg -n "(^|[^a-z_])(buf|port|fifo|skid)[[:space:]]+(int|uint|bit|logic|signed|unsigned|[a-z_][a-z0-9_]*[[])" tests/syntax_test.c tests/resolve_test.c
rg -n "PIGEN_DATA_TYPE_BYTE|pigen_data_type_byte|data_type_is_byte" include src tests
```

Expected: all searches print nothing. Do not run the transfer-first search over production fixtures, which are intentionally quarantined until vertical lowering.

- [ ] **Step 6: Commit hardening tests and fixes**

```sh
git add tests/syntax_test.c tests/resolve_test.c src/syntax.c src/resolve.c
git commit -m "harden declaration compatibility boundary"
```

---

### Task 6: Cut over current documentation and durable notes

**Files:**

- Modify: `README.md`
- Modify: `SPEC.md`
- Modify: `PLAN.md`
- Modify: `agent_notes/COMPILER_ARCHITECTURE.md`
- Modify: `agent_notes/SEMANTIC_INVARIANTS.md`
- Modify: `agent_notes/signal_model.md`
- Modify: `agent_notes/readme_maintenance.md`

- [ ] **Step 1: Replace the public language description**

In `README.md` and `SPEC.md`:

- list `int[n]`, `uint[n]`, and `bit` as the current Pigen primitive data types;
- use `bit[8]` for neutral eight-bit examples;
- state data type × transfer type × declarator shape explicitly;
- show `int[16] fifo[8] pending[lanes];` as the depth/shape separation example;
- document abstract unqualified Pigen inputs and required internal/output realization;
- preserve ordinary SystemVerilog `byte` as a distinct signed SV type, not a Pigen alias;
- remove public transfer-first examples; and
- state clearly that the production executable cutover comes with vertical RTL lowering.

Do not edit historical approved design/implementation documents merely because they record the superseded `byte` decision; they are historical evidence, not current language documentation.

- [ ] **Step 2: Update the linear roadmap**

In `PLAN.md`, mark source-visible data-first declarations, shared declaration topology, abstract input policy, descriptor-owned FIFO depth, and Pigen `byte` removal complete. Keep production integration, RTL IR, adapters, pipeline migration, and emission unchecked. Make the next step the narrow semantic-to-elastic-RTL vertical slice rather than another frontend bridge.

- [ ] **Step 3: Compact and correct the agent notes**

Record Ari's 2026-08-27 implementation boundary and the enduring laws:

- every runtime datum remains data type × transfer type × declarator shape;
- written transfer occurrence and omission are distinct syntax states;
- transfer arguments are descriptor-owned and never payload dimensions;
- unqualified-transfer policy belongs to the data-type owner;
- declaration resolution never enumerates primitive families;
- `byte` is not a Pigen primitive; use `bit[8]` for neutral storage;
- production fixtures remain on the prototype parser only until vertical lowering.

Prune repeated plans, stale “next syntax slice” prose, old Pigen-byte rules, and duplicate architecture narration. Keep the notes compact enough to survive context compaction usefully.

- [ ] **Step 4: Run current-document removal checks**

Run:

```sh
rg -n 'Pigen.*byte|byte.*Pigen|`byte` is|int\[n\].*uint\[n\].*bit.*byte' README.md SPEC.md PLAN.md agent_notes
rg -n "^(input |output |inout )?(buf|port|fifo|skid)[[:space:]]" README.md SPEC.md agent_notes
git diff --check
```

Expected: no current document calls `byte` a Pigen type and no current public example uses transfer-first Pigen syntax. A sentence distinguishing ordinary SystemVerilog `byte` is expected and should be manually inspected rather than mechanically deleted.

- [ ] **Step 5: Commit the documentation cutover**

```sh
git add README.md SPEC.md PLAN.md agent_notes
git commit -m "document data-first declaration model"
```

---

### Task 7: Verify the complete cutover and review the branch

**Files:**

- Inspect: all changed files
- Modify only if verification or review identifies a concrete defect

- [ ] **Step 1: Run all focused structured-frontend targets from current sources**

Run:

```sh
make transfer-type-test syntax-model-test semantic-test expression-resolve-test expression-use-test resolve-test
```

Expected: every focused target passes.

- [ ] **Step 2: Run the complete clean verification suite**

Run:

```sh
make clean
make verify
```

Expected: the complete structured, production, waveform, and ordinary-SystemVerilog compatibility suite passes. This verifies that the shared frontend changed while the production prototype remained behaviorally intact.

- [ ] **Step 3: Run final architectural searches**

Run:

```sh
rg -n "PIGEN_SYNTAX_STATIC_SIGNAL|static_signal_declaration|static_signal_declarator" include src tests
rg -n "PIGEN_DATA_TYPE_BYTE|pigen_data_type_byte|data_type_is_byte" include src tests README.md SPEC.md PLAN.md agent_notes
rg -n "nonstatic_transfer_type_at|add_static_signal_declaration|resolve_static_transfer_type" src include tests
git diff --check
git status --short
```

Expected: all removal searches print nothing, the diff check succeeds, and status contains only deliberate work.

- [ ] **Step 4: Review against the approved design**

Use `superpowers:requesting-code-review`. Review specifically for:

- concrete primitive enumeration outside `data_type.c`;
- concrete transfer-parameter interpretation outside the descriptor boundary;
- parser rollback which omits any arena;
- accidental production-path edits;
- any second static/nonstatic declaration topology;
- hidden acceptance of transfer-first Pigen; and
- stale current documentation or notes.

- [ ] **Step 5: Apply verified review fixes and rerun their owning tests**

Use `superpowers:receiving-code-review` for each substantive finding. Reproduce the issue with the smallest focused test, fix it in its owner, rerun that target, then rerun `make verify` if code changed.

- [ ] **Step 6: Perform completion verification and commit any final fixes**

Use `superpowers:verification-before-completion`. If review produced changes:

Inspect `git diff --name-only`, stage only the files changed for verified review
findings, then run:

```sh
git commit -m "address declaration cutover review"
make clean
make verify
git status --short
```

Do not claim completion from an earlier run. The final report must cite the fresh verification result, the production-integration boundary, the commits created, and the next planned vertical RTL-lowering slice.
