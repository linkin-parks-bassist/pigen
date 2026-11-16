---
status: green
revised_at: "2026-11-15T08:20:24+11:00"
---

Structured instance emission is owned by finished RTL records and the terminal backend. pigen_rtl_instance carries primitive enum, payload type, parameter/connection ranges, owner/provenance and final name. A non-primitive module instance may additionally own a resolved definition string and semantic_instance ID. Names are finished by lowering or the name allocator; rendering does not reconstruct them from source scopes.

src/sv_emit.c maps primitive enums to pigen definitions, emits PAYLOAD_T from the resolved RTL type, FIFO depth as DEPTH (lowered to its signed 32-bit int interface) and merge source count as N. Generic module instances emit their owned definition and instance names with named connection records. Connections carry final port names, expressions/objects and child port direction. OUTPUT/INOUT expression connections use lvalue rendering; signed selected DATA therefore stays writable instead of acquiring a value cast. Inputs retain value typing. No backend path consults semantic symbols, transfer types or generated text to infer protocol roles.

Legacy instances with neither primitive enum nor explicit definition retain the positional parameter/object rendering used by record-level tests. That fallback does not describe production primitive or generated-child interfaces.

Primitive payload spelling preserves resolved packed dimensions and direction consistently with object declarations. The backend consumes finished bounds; it does not reconstruct semantic types.

Evidence: include/pigen/rtl.h, src/rtl.c, src/rtl_lower.c and src/sv_emit.c; make rtl-test, sv-emit-test, generated-instance-test and the simulation gates in make verify. Generated child transport includes signed and multidimensional packed DATA across all native AXI families (how/are/generated/child/instances/lowered.md). The full renderer contract is what/is/the/terminal/systemverilog/emitter/contract.md.
