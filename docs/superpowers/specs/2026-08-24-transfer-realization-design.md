# Transfer realization design

## Purpose

The replacement compiler middle already assigns every signal a canonical
transfer type and uses one descriptor catalogue for its semantic laws. Before
the first structured RTL slice, that owner must also state what implementation
family realizes each transfer type. Otherwise the lowerer would need to recover
the concrete transfer catalogue and become a second source of language meaning.

This change establishes a backend-neutral semantic-to-RTL boundary. It does not
create RTL IR, emit SystemVerilog, or link the replacement middle into the
production executable.

## Boundary

`transfer_type.h` exposes a closed `pigen_transfer_realization` enum. Each
canonical `pigen_transfer_type_descriptor` names exactly one realization:

| Transfer type | Realization |
| --- | --- |
| `abstract` | boundary |
| `wire` | combinational net |
| `reg` | procedural variable |
| `logic` | procedural variable |
| `buf` | elastic slot |
| `port` | pulse register |
| `fifo` | parameterized queue |
| `skid` | skid queue |

The realization vocabulary describes implementation families rather than
source-language identities. Multiple transfer types may share a realization,
as `reg` and `logic` do. The distinct buffered families remain distinct because
their storage and transfer behavior differ materially.

A focused realization descriptor supplies only lowering-relevant semantic
facts:

- storage capacity form: none, fixed, or supplied by the transfer argument;
- fixed capacity where applicable;
- ready dependency: external boundary, constant, downstream-combinational, or
  registered occupancy;
- whether occupancy state exists;
- reset behavior: none, user-controlled procedural state, or automatically
  emptied transfer storage.

The initial realization properties are fixed as follows:

| Realization | Capacity | Ready dependency | Occupancy | Reset |
| --- | --- | --- | --- | --- |
| boundary | none | external boundary | no | none |
| combinational net | none | constant | no | none |
| procedural variable | none | constant | no | user-controlled |
| elastic slot | fixed one | downstream-combinational | yes | empty |
| pulse register | fixed one | constant | no | empty |
| parameterized queue | transfer argument | registered occupancy | yes | empty |
| skid queue | fixed two | registered occupancy | yes | empty |

Capacity describes consumable transfer storage, not whether payload bits happen
to reside in a register. A procedural variable therefore has no transfer
capacity, while a pulse register has one payload slot but no occupancy-based
backpressure.

Existing transfer-type descriptor fields continue to own source spelling,
concreteness, static classification, parameter form, write eligibility,
valid/ready constants, consumption, production, ownership, and domain binding.
The realization record does not duplicate those laws.

Neither descriptor contains SystemVerilog module names, generated identifiers,
port conventions, rendered declarations, or backend syntax. The future RTL
lowerer maps realization identities to RTL-IR objects; the SystemVerilog emitter
will render those objects without seeing source transfer types.

## Invariants

- Every concrete transfer type has exactly one realizable implementation
  family.
- `abstract` maps only to the boundary realization. RTL lowering must reject an
  abstract signal that has not been specialized.
- Boundary readiness is external until specialization; it is never silently
  treated as a constant or a local ready equation.
- A descriptor's transfer argument and realization agree: only a realization
  whose capacity comes from the transfer argument may use the depth parameter.
- Fixed capacities are structural properties of the realization, not magic
  constants in a lowerer.
- General syntax, resolution, expression, incidence, ownership, and domain
  passes query transfer laws and never enumerate realization families.
- RTL lowering enumerates realization families at one explicit boundary and
  never switches on `pigen_transfer_type`.
- No pass rediscovers a realization from source spelling or emitted primitive
  names.

## Extension test

Adding a transfer type which uses an existing realization requires:

1. one transfer-type enum value;
2. one canonical descriptor entry containing its laws and realization;
3. focused catalogue tests and language documentation.

It must not require changes to general semantic passes or RTL lowering.

A genuinely new implementation family additionally requires one realization
enum value and one implementation in the semantic-to-RTL adapter. That adapter
is the only downstream exhaustive switch. If the new type introduces a law the
current descriptor cannot express, the transfer-type owner gains a focused
field or query; unrelated passes do not gain concrete-type branches.

## Errors and provenance

Catalogue construction admits no partially described concrete transfer type.
Public queries reject invalid enum values rather than returning a plausible
default. The current slice introduces no new source diagnostic. When RTL
lowering is added, unresolved boundary realization will be diagnosed using the
signal's existing source span; no generated location will be fabricated.

## Verification

Focused transfer-type tests will assert:

- the realization selected by every transfer type;
- shared realization of `reg` and `logic`;
- distinct realizations for `buf`, `port`, `fifo`, and `skid`;
- capacity, ready-dependency, occupancy, and reset properties;
- agreement between FIFO's depth argument and parameterized capacity;
- rejection of invalid transfer-type and realization identities.

The implementation will then run `make verify` to demonstrate that the
catalogue extension changes no production behavior.

## Scope

The implementation changes `include/pigen/transfer_type.h`,
`src/transfer_type.c`, focused tests, `PLAN.md`, and compact architecture notes.
It does not change production prototype descriptors, storage primitives,
generated RTL, accepted syntax, or `main()`.

Design organized by Ari with David on 2026-08-24.
