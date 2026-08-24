# Transfer Realization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every canonical transfer type a complete, backend-neutral realization family and property record which future RTL lowering can consume without enumerating source transfer types.

**Architecture:** `transfer_type.c` remains the sole owner of both source transfer laws and their realization mapping. Public code sees opaque enum identities plus focused descriptors; future lowering switches once over realization identities, while syntax and semantic passes continue to query transfer laws generically.

**Tech Stack:** C17, designated-initializer compile-time catalogues, assert-based C tests, GNU Make.

**Spec:** `docs/superpowers/specs/2026-08-24-transfer-realization-design.md`

## Global Constraints

- Pigen is pre-release: no legacy syntax, fallback, feature flag, or compatibility path.
- Ordinary accepted SystemVerilog behavior must remain unchanged.
- All C identifiers use lowercase snake_case; preprocessor definitions and enum constants use uppercase snake case.
- The realization model contains no SystemVerilog module names, generated names, rendered declarations, or backend port conventions.
- General syntax, resolution, expression, incidence, ownership, and domain passes must not enumerate realization families.
- A transfer type reusing an existing realization changes only the transfer-type enum, canonical descriptor entry, focused tests, and language documentation.
- A genuinely new realization family is handled only by the transfer-type owner and the future semantic-to-RTL adapter.
- Invalid zero values must make omitted catalogue initializers fail closed rather than appear valid.

---

## File structure

- `include/pigen/transfer_type.h`: public realization identities, property identities, descriptor records, and query declarations.
- `src/transfer_type.c`: the only realization-property catalogue and the only mapping from source transfer types to realization identities.
- `tests/transfer_type_test.c`: exhaustive catalogue mappings, property laws, shared/distinct realization checks, and invalid-identity behavior.
- `PLAN.md`: completed phase-one item and current status.
- `agent_notes/COMPILER_ARCHITECTURE.md`: durable architectural boundary and extension test.
- `agent_notes/SEMANTIC_INVARIANTS.md`: semantic ownership invariant for realization.
- `agent_notes/signal_model.md`: current cutover record and remaining work.

### Task 1: Canonical transfer realization catalogue

**Files:**

- Modify: `include/pigen/transfer_type.h:17-41`
- Modify: `src/transfer_type.c:6-129`
- Test: `tests/transfer_type_test.c:13-63`

**Interfaces:**

- Consumes: existing `pigen_transfer_type`, `pigen_transfer_parameter`, and `pigen_transfer_type_descriptor_get()` interfaces.
- Produces: `pigen_transfer_realization`, `pigen_transfer_capacity_source`, `pigen_transfer_ready_dependency`, `pigen_transfer_reset`, `pigen_transfer_realization_descriptor`, `pigen_transfer_realization_is_valid()`, and `pigen_transfer_realization_descriptor_get()`.
- Extends: `pigen_transfer_type_descriptor` with `pigen_transfer_realization realization`.

- [ ] **Step 1: Write exhaustive failing realization tests**

Add a helper to `tests/transfer_type_test.c`:

```c
static const pigen_transfer_realization_descriptor *realization_for(
	const pigen_transfer_type_descriptor *transfer_type)
{
	assert(transfer_type);
	return pigen_transfer_realization_descriptor_get(
		transfer_type->realization);
}
```

Extend `main()` to retrieve descriptors for every transfer type and every
realization, then add these assertions:

```c
const pigen_transfer_type_descriptor *reg_descriptor =
	pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_REG);
const pigen_transfer_type_descriptor *logic_descriptor =
	pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_LOGIC);
const pigen_transfer_type_descriptor *skid_descriptor =
	pigen_transfer_type_descriptor_get(PIGEN_TRANSFER_TYPE_SKID);
const pigen_transfer_realization_descriptor *boundary =
	realization_for(abstract_descriptor);
const pigen_transfer_realization_descriptor *net =
	realization_for(wire_descriptor);
const pigen_transfer_realization_descriptor *variable =
	realization_for(reg_descriptor);
const pigen_transfer_realization_descriptor *elastic =
	realization_for(buf_descriptor);
const pigen_transfer_realization_descriptor *pulse =
	realization_for(port_descriptor);
const pigen_transfer_realization_descriptor *queue =
	realization_for(fifo_descriptor);
const pigen_transfer_realization_descriptor *skid =
	realization_for(skid_descriptor);

assert(abstract_descriptor->realization ==
	PIGEN_TRANSFER_REALIZATION_BOUNDARY);
assert(wire_descriptor->realization ==
	PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET);
assert(reg_descriptor->realization ==
	PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE);
assert(logic_descriptor->realization == reg_descriptor->realization);
assert(buf_descriptor->realization ==
	PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT);
assert(port_descriptor->realization ==
	PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER);
assert(fifo_descriptor->realization ==
	PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE);
assert(skid_descriptor->realization ==
	PIGEN_TRANSFER_REALIZATION_SKID_QUEUE);

assert(boundary &&
	boundary->capacity_source == PIGEN_TRANSFER_CAPACITY_NONE &&
	boundary->ready_dependency == PIGEN_TRANSFER_READY_EXTERNAL &&
	!boundary->has_occupancy &&
	boundary->reset == PIGEN_TRANSFER_RESET_NONE);
assert(net &&
	net->capacity_source == PIGEN_TRANSFER_CAPACITY_NONE &&
	net->ready_dependency == PIGEN_TRANSFER_READY_CONSTANT &&
	!net->has_occupancy && net->reset == PIGEN_TRANSFER_RESET_NONE);
assert(variable &&
	variable->capacity_source == PIGEN_TRANSFER_CAPACITY_NONE &&
	variable->ready_dependency == PIGEN_TRANSFER_READY_CONSTANT &&
	!variable->has_occupancy &&
	variable->reset == PIGEN_TRANSFER_RESET_PROCEDURAL);
assert(elastic &&
	elastic->capacity_source == PIGEN_TRANSFER_CAPACITY_FIXED &&
	elastic->fixed_capacity == 1 &&
	elastic->ready_dependency == PIGEN_TRANSFER_READY_DOWNSTREAM &&
	elastic->has_occupancy && elastic->reset == PIGEN_TRANSFER_RESET_EMPTY);
assert(pulse &&
	pulse->capacity_source == PIGEN_TRANSFER_CAPACITY_FIXED &&
	pulse->fixed_capacity == 1 &&
	pulse->ready_dependency == PIGEN_TRANSFER_READY_CONSTANT &&
	!pulse->has_occupancy && pulse->reset == PIGEN_TRANSFER_RESET_EMPTY);
assert(queue &&
	queue->capacity_source == PIGEN_TRANSFER_CAPACITY_ARGUMENT &&
	queue->fixed_capacity == 0 &&
	queue->ready_dependency == PIGEN_TRANSFER_READY_OCCUPANCY &&
	queue->has_occupancy && queue->reset == PIGEN_TRANSFER_RESET_EMPTY &&
	fifo_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_DEPTH);
assert(skid &&
	skid->capacity_source == PIGEN_TRANSFER_CAPACITY_FIXED &&
	skid->fixed_capacity == 2 &&
	skid->ready_dependency == PIGEN_TRANSFER_READY_OCCUPANCY &&
	skid->has_occupancy && skid->reset == PIGEN_TRANSFER_RESET_EMPTY);

assert(pigen_transfer_realization_is_valid(
	PIGEN_TRANSFER_REALIZATION_BOUNDARY));
assert(pigen_transfer_realization_is_valid(
	PIGEN_TRANSFER_REALIZATION_SKID_QUEUE));
assert(!pigen_transfer_realization_is_valid(
	PIGEN_TRANSFER_REALIZATION_INVALID));
assert(!pigen_transfer_realization_is_valid(
	(pigen_transfer_realization)-1));
assert(!pigen_transfer_realization_is_valid(
	(pigen_transfer_realization)99));
assert(!pigen_transfer_realization_descriptor_get(
	PIGEN_TRANSFER_REALIZATION_INVALID));

assert(buf_descriptor->realization != port_descriptor->realization);
assert(buf_descriptor->realization != fifo_descriptor->realization);
assert(buf_descriptor->realization != skid_descriptor->realization);
assert(port_descriptor->realization != fifo_descriptor->realization);
assert(port_descriptor->realization != skid_descriptor->realization);
assert(fifo_descriptor->realization != skid_descriptor->realization);

for (pigen_transfer_type transfer_type = PIGEN_TRANSFER_TYPE_ABSTRACT;
	transfer_type <= PIGEN_TRANSFER_TYPE_SKID; transfer_type++)
{
	const pigen_transfer_type_descriptor *transfer_descriptor =
		pigen_transfer_type_descriptor_get(transfer_type);
	const pigen_transfer_realization_descriptor *realization =
		realization_for(transfer_descriptor);
	assert((transfer_descriptor->parameter == PIGEN_TRANSFER_PARAMETER_DEPTH) ==
		(realization->capacity_source == PIGEN_TRANSFER_CAPACITY_ARGUMENT));
}
```

- [ ] **Step 2: Run the focused test and confirm the interface is absent**

Run:

```bash
make transfer-type-test
```

Expected: compilation fails because `pigen_transfer_realization_descriptor`,
`pigen_transfer_realization_descriptor_get`, and the realization enum constants
are not yet declared.

- [ ] **Step 3: Add the public realization vocabulary**

Insert the following definitions after `pigen_transfer_parameter` in
`include/pigen/transfer_type.h`:

```c
typedef enum {
	PIGEN_TRANSFER_REALIZATION_INVALID,
	PIGEN_TRANSFER_REALIZATION_BOUNDARY,
	PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET,
	PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE,
	PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT,
	PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER,
	PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE,
	PIGEN_TRANSFER_REALIZATION_SKID_QUEUE
} pigen_transfer_realization;

typedef enum {
	PIGEN_TRANSFER_CAPACITY_INVALID,
	PIGEN_TRANSFER_CAPACITY_NONE,
	PIGEN_TRANSFER_CAPACITY_FIXED,
	PIGEN_TRANSFER_CAPACITY_ARGUMENT
} pigen_transfer_capacity_source;

typedef enum {
	PIGEN_TRANSFER_READY_INVALID,
	PIGEN_TRANSFER_READY_EXTERNAL,
	PIGEN_TRANSFER_READY_CONSTANT,
	PIGEN_TRANSFER_READY_DOWNSTREAM,
	PIGEN_TRANSFER_READY_OCCUPANCY
} pigen_transfer_ready_dependency;

typedef enum {
	PIGEN_TRANSFER_RESET_INVALID,
	PIGEN_TRANSFER_RESET_NONE,
	PIGEN_TRANSFER_RESET_PROCEDURAL,
	PIGEN_TRANSFER_RESET_EMPTY
} pigen_transfer_reset;

typedef struct {
	pigen_transfer_capacity_source capacity_source;
	size_t fixed_capacity;
	pigen_transfer_ready_dependency ready_dependency;
	int has_occupancy;
	pigen_transfer_reset reset;
} pigen_transfer_realization_descriptor;
```

Add this field immediately after `parameter` in
`pigen_transfer_type_descriptor`:

```c
	pigen_transfer_realization realization;
```

Add these declarations before the existing transfer-type queries:

```c
const pigen_transfer_realization_descriptor *
pigen_transfer_realization_descriptor_get(
	pigen_transfer_realization realization);
int pigen_transfer_realization_is_valid(
	pigen_transfer_realization realization);
```

- [ ] **Step 4: Implement the realization-property catalogue**

Insert this private table before `transfer_types` in `src/transfer_type.c`:

```c
static const pigen_transfer_realization_descriptor realizations[] = {
	[PIGEN_TRANSFER_REALIZATION_BOUNDARY] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_NONE,
		.ready_dependency = PIGEN_TRANSFER_READY_EXTERNAL,
		.reset = PIGEN_TRANSFER_RESET_NONE
	},
	[PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_NONE,
		.ready_dependency = PIGEN_TRANSFER_READY_CONSTANT,
		.reset = PIGEN_TRANSFER_RESET_NONE
	},
	[PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_NONE,
		.ready_dependency = PIGEN_TRANSFER_READY_CONSTANT,
		.reset = PIGEN_TRANSFER_RESET_PROCEDURAL
	},
	[PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_FIXED,
		.fixed_capacity = 1,
		.ready_dependency = PIGEN_TRANSFER_READY_DOWNSTREAM,
		.has_occupancy = 1,
		.reset = PIGEN_TRANSFER_RESET_EMPTY
	},
	[PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_FIXED,
		.fixed_capacity = 1,
		.ready_dependency = PIGEN_TRANSFER_READY_CONSTANT,
		.reset = PIGEN_TRANSFER_RESET_EMPTY
	},
	[PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_ARGUMENT,
		.ready_dependency = PIGEN_TRANSFER_READY_OCCUPANCY,
		.has_occupancy = 1,
		.reset = PIGEN_TRANSFER_RESET_EMPTY
	},
	[PIGEN_TRANSFER_REALIZATION_SKID_QUEUE] = {
		.capacity_source = PIGEN_TRANSFER_CAPACITY_FIXED,
		.fixed_capacity = 2,
		.ready_dependency = PIGEN_TRANSFER_READY_OCCUPANCY,
		.has_occupancy = 1,
		.reset = PIGEN_TRANSFER_RESET_EMPTY
	}
};
```

Implement fail-closed lookup before `pigen_transfer_type_is_valid()`:

```c
const pigen_transfer_realization_descriptor *
pigen_transfer_realization_descriptor_get(
	pigen_transfer_realization realization)
{
	const pigen_transfer_realization_descriptor *descriptor;

	if ((size_t)realization >=
		sizeof(realizations) / sizeof(*realizations)) return NULL;
	descriptor = &realizations[realization];
	if (descriptor->capacity_source == PIGEN_TRANSFER_CAPACITY_INVALID ||
		descriptor->ready_dependency == PIGEN_TRANSFER_READY_INVALID ||
		descriptor->reset == PIGEN_TRANSFER_RESET_INVALID) return NULL;
	if ((descriptor->capacity_source == PIGEN_TRANSFER_CAPACITY_FIXED) !=
		(descriptor->fixed_capacity != 0)) return NULL;
	return descriptor;
}

int pigen_transfer_realization_is_valid(
	pigen_transfer_realization realization)
{
	return pigen_transfer_realization_descriptor_get(realization) != NULL;
}
```

This validation makes every zero-initialized omitted entry invalid. It also
requires a positive fixed capacity exactly when the capacity source is fixed.

- [ ] **Step 5: Map every transfer type and make transfer lookup fail closed**

Add these designated fields to the existing `transfer_types` entries:

```c
/* PIGEN_TRANSFER_TYPE_ABSTRACT */
.realization = PIGEN_TRANSFER_REALIZATION_BOUNDARY,
/* PIGEN_TRANSFER_TYPE_WIRE */
.realization = PIGEN_TRANSFER_REALIZATION_COMBINATIONAL_NET,
/* PIGEN_TRANSFER_TYPE_REG */
.realization = PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE,
/* PIGEN_TRANSFER_TYPE_LOGIC */
.realization = PIGEN_TRANSFER_REALIZATION_PROCEDURAL_VARIABLE,
/* PIGEN_TRANSFER_TYPE_BUF */
.realization = PIGEN_TRANSFER_REALIZATION_ELASTIC_SLOT,
/* PIGEN_TRANSFER_TYPE_PORT */
.realization = PIGEN_TRANSFER_REALIZATION_PULSE_REGISTER,
/* PIGEN_TRANSFER_TYPE_FIFO */
.realization = PIGEN_TRANSFER_REALIZATION_PARAMETERIZED_QUEUE,
/* PIGEN_TRANSFER_TYPE_SKID */
.realization = PIGEN_TRANSFER_REALIZATION_SKID_QUEUE,
```

Replace `pigen_transfer_type_is_valid()` with:

```c
int pigen_transfer_type_is_valid(pigen_transfer_type transfer_type)
{
	return (size_t)transfer_type <
		sizeof(transfer_types) / sizeof(*transfer_types) &&
		pigen_transfer_realization_is_valid(
			transfer_types[transfer_type].realization);
}
```

This ensures a newly inserted transfer-type enum value with no descriptor entry
is invalid instead of inheriting a plausible zero-valued realization.

- [ ] **Step 6: Run the focused test and inspect compiler warnings**

Run:

```bash
make transfer-type-test
```

Expected: compilation succeeds with `-Werror`, and the executable prints:

```text
PASS: transfer types have one descriptor catalogue
```

- [ ] **Step 7: Audit the implementation boundary**

Run:

```bash
git diff --check
git diff --name-only
```

Expected changed files at this point:

```text
include/pigen/transfer_type.h
src/transfer_type.c
tests/transfer_type_test.c
```

Confirm that the diff contains no backend module spelling such as
`pigen_buf`, `pigen_fifo`, `pigen_port`, or `pigen_skid`.

- [ ] **Step 8: Commit the realization catalogue**

```bash
git add include/pigen/transfer_type.h src/transfer_type.c tests/transfer_type_test.c
git commit -m "model transfer type realizations"
```

### Task 2: Record the completed boundary and verify the repository

**Files:**

- Modify: `PLAN.md:48-90,114-115`
- Modify: `agent_notes/COMPILER_ARCHITECTURE.md:214-242`
- Modify: `agent_notes/SEMANTIC_INVARIANTS.md:210-220`
- Modify: `agent_notes/signal_model.md:103-123`

**Interfaces:**

- Consumes: the realization identities and properties implemented by Task 1.
- Produces: an accurate execution plan and compact durable notes identifying the next work as phase two of the architecture cutover.

- [ ] **Step 1: Mark the phase-one representation item complete**

In `PLAN.md`, change the unchecked realization item to:

```markdown
- [x] Give each transfer type a precise storage and lowering representation;
  distinct backend-neutral realization identities describe boundaries, nets,
  variables, elastic slots, pulse registers, parameterized queues, and skid
  queues.
```

In the current-status section, add this concise result after the canonical
transfer-type descriptor paragraph:

```markdown
The same owner now maps each transfer type to a backend-neutral realization
identity with explicit capacity source, ready dependency, occupancy, and reset
properties. Invalid zero-valued properties make incomplete catalogue entries
fail closed. Backend spellings remain outside the semantic catalogue.
```

- [ ] **Step 2: Compact the architecture notes around the completed result**

Replace the stale incomplete-status and approved-next-slice paragraphs in
`agent_notes/COMPILER_ARCHITECTURE.md` with:

```markdown
The replacement middle now has one signal identity arena and one symbol
binding for statics and the other transfer types. Expression-use analysis and
direct-transfer incidence retain every participating signal. Canonical shape
identities and recognized declarator shapes are explicit as well.

The transfer-type owner maps each source transfer type to a backend-neutral
realization identity. Focused realization descriptors state capacity source,
ready dependency, occupancy, and reset behavior without naming SystemVerilog
primitives or generated interfaces. Reusing a realization requires only a new
transfer-type catalogue entry; a genuinely new realization additionally
extends the single future semantic-to-RTL adapter. General semantic passes do
not enumerate either catalogue.
```

Keep the design link as one final sentence rather than a chronological diary:

```markdown
The realization boundary is specified in
`docs/superpowers/specs/2026-08-24-transfer-realization-design.md`.
```

- [ ] **Step 3: Update the durable semantic and signal-model invariants**

In `agent_notes/SEMANTIC_INVARIANTS.md`, replace the sentence saying precise
realization must still join the owner with:

```markdown
- A signal's transfer-type descriptor names one backend-neutral realization.
  The realization owner states capacity source, ready dependency, occupancy,
  and reset behavior. General semantic passes do not enumerate realization
  families, and the future RTL adapter does not switch on source transfer
  types.
```

In `agent_notes/signal_model.md`, replace the stale realization warning and
remaining-work reference with a compact cutover record:

```markdown
The canonical descriptor catalogue now also maps each transfer type to a
backend-neutral realization identity. Boundaries, combinational nets,
procedural variables, elastic slots, pulse registers, parameterized queues,
and skid queues retain distinct structural meaning. Capacity source, ready
dependency, occupancy, and reset behavior are explicit properties; backend
module names and generated interfaces are not semantic data.

After the realization cutover, phase one of `PLAN.md` is complete. The next
work is the centralized primitive data-type algebra and the remaining shared
frontend required by the first vertical RTL slice.
```

Retain the existing historical signal-arena, shape, and incidence record.

- [ ] **Step 4: Run focused and full verification**

Run:

```bash
make transfer-type-test
make verify
```

Expected: the focused test prints its existing `PASS` line; every compiler,
simulation, waveform, syntax, semantic, and compatibility target in
`make verify` exits successfully.

- [ ] **Step 5: Review the completed diff and repository state**

Run:

```bash
git diff --check
git status --short
git diff --stat HEAD~1
```

Expected: only the four documentation files in this task are uncommitted; the
implementation commit from Task 1 remains intact. Confirm that stale claims
that realization is incomplete are absent:

```bash
rg -n "still needs a precise storage|does not yet encode precise storage|storage/lowering laws.*remain incomplete" PLAN.md agent_notes
```

Expected: no matches.

- [ ] **Step 6: Commit the completed architecture record**

```bash
git add PLAN.md agent_notes/COMPILER_ARCHITECTURE.md agent_notes/SEMANTIC_INVARIANTS.md agent_notes/signal_model.md
git commit -m "record transfer realization cutover"
```
