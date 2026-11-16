---
status: green
revised_at: "2026-11-17T10:50:28+11:00"
---

include/pigen/rtl.h exposes an owned pigen_rtl_source_reference arena in pigen_rtl_model. A reference records module, semantic scope/signal provenance, source_path, emitted_name, role and an RTL object or instance identity. Roles distinguish selected scopes, values, VALID/READY, storage-input sides, native fields, child instances and instance ports. Scope records carry the flattened lexical-name prefix. One channel can map to several interface/storage objects; aliases retain separate references.

src/rtl_lower.c:lower_source_references publishes the relation during pigen_lower_rtl_module after declarations and generated instances receive terminal names. Paths are module-local and include external generate names, signed iteration coordinates and declared native array coordinates, for example genblk1.lanes[-1].child.s.aw. Native AXI lives on module/fabric ports and child views; ordinary local buffers have ordinary value paths. beat is the canonical Stream channel. Fields append their names; child paths include instance and declared port. Native child pins map channel/field paths to instanceName.standard_pin using resolved contracts and connection records. Ordinary child ports retain their names.

Controls and storage roles are metadata distinctions. Anonymous procedural blocks add no authored path component, so shadowed locals can share source_path text while scope/signal identities distinguish their references and private emitted names. Consumers must retain identities instead of keying only by path text.

pigen_rtl_source_reference_add validates module/target ownership and role shape, owns both strings and returns a stable arena index. Identical publication is idempotent; conflicts or invalid requests return SIZE_MAX without appending. Accessors bounds-check indices. Cleanup frees strings and the arena; consumers can inspect it after upstream models are released. Synthetic objects lack source references; raw retained SV keeps its authored namespace.

rtl_module_for_semantic checks the RTL arena after its memo lookup, recovering an existing module owner when composition reset the memo. Unowned pigen_rtl_module_add records have invalid semantic provenance. This prevents duplicate owners and preserves target identities.

tests/generated_instance_test.c checks all families through anonymous outer scopes, negative-index loops, ordinary local buffers, forward sibling chains, typed child views, standard fields and ordinary bound ports. It verifies paths, ownership, idempotence, conflicts, invalid handles and string lifetime. what/are/automatic/generate/block/names.md owns external names; how/are/generated/child/instances/lowered.md owns generated lowering. Source references preserve authored lookup and interface behavior; broader retained/native namespace and driver integration remain work.
